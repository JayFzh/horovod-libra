// Copyright 2016 The TensorFlow Authors. All Rights Reserved.
// Modifications copyright (C) 2019 Uber Technologies, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================

#include "gpu_operations.h"

#include <thread>

namespace horovod {
namespace common {

GPUOpContext::GPUOpContext(GPUContext* context, HorovodGlobalState* global_state)
    : gpu_context_(context), global_state_(global_state) {
      // for(int i=0;i<5;i++){
      //   gpuStream_t new_stream = gpu_context_->streams[global_state_->current_nccl_stream][i];
      //   if (new_stream == nullptr) {
      //     std::cout<<"create stream "<<i<<"!!\n";
      //     gpu_context_->StreamCreate(&new_stream);
      //   }
      //   this->para_streams.push_back(&new_stream);
      // }
    }

void GPUOpContext::InitGPU(const std::vector<TensorTableEntry>& entries,bool is_allreduce,bool is_para) {
  auto& first_entry = entries[0];
  gpu_context_->SetDevice(first_entry.device);
  int stream_index = first_entry.device;
  if(is_allreduce){
    stream_index =  global_state_->stream_assignment[global_state_->current_gpu_stream]; 
    if(is_para) {
      stream_index = 8;
    }
    // gpuStream_t& stream = gpu_context_->streams[global_state_->current_nccl_stream][stream_index];
    // this->stream = this->para_streams[stream_index];
    // if(this->stream == NULL) std::cout<<"NULL STREAM!!!\n";
  }
  gpuStream_t& stream = gpu_context_->streams[global_state_->current_nccl_stream][stream_index];
  if (stream == nullptr) {
    gpu_context_->StreamCreate(&stream);
  }
  // Ensure stream is in the map before executing reduction.
}

// fzh-alloc
void GPUOpContext::InitNewStream(int times){
  times = (times % 10) + 3;
  gpuStream_t* new_stream_ = &gpu_context_->streams[global_state_->current_nccl_stream][times];
  if(new_stream_ == nullptr){
    gpu_context_->StreamCreate(new_stream_);
  }
  this->new_stream = new_stream_;
}


void GPUOpContext::InitGPUQueue(const std::vector<TensorTableEntry>& entries, const Response& response,bool is_allreduce,bool is_para) {
  event_queue = std::queue<std::pair<std::string, gpuEvent_t>>();
  int stream_index = entries[0].device;
  if(is_allreduce){
    stream_index =  global_state_->stream_assignment[global_state_->current_gpu_stream];
    if(is_para) {
      stream_index = 8;
    } 
    global_state_->stream_index = stream_index;
  }
  this->stream = &gpu_context_->streams[global_state_->current_nccl_stream][stream_index];
  if(is_allreduce){
    printf("all reduce stream id %d\n",stream_index);
  }
  else{
    printf("not all reduce stream id %d\n",stream_index);
  }
  if (global_state_->timeline.Initialized()) {
    gpu_context_->RecordEvent(event_queue, QUEUE, *stream);
  }
  if(is_allreduce){
    global_state_->current_gpu_stream = (global_state_->current_gpu_stream+1)%(global_state_->stream_assignment.size());
  }
}

Status GPUOpContext::FinalizeGPUQueue(const std::vector<TensorTableEntry>& entries, bool free_host_buffer /*= true*/,
                                      const std::function<void()>& error_check_callback) {
  // Use completion marker via event because it's faster than blocking gpuStreamSynchronize() in this thread.
  gpu_context_->RecordEvent(event_queue, "", *stream);

  auto& first_entry = entries[0];
  void* cpu_buffer = host_buffer;
  auto& evt_queue = event_queue;
  // auto& evt_queue_fzh = event_queue_fzh;
  auto& timeline = global_state_->timeline;
  auto& gpu_context = gpu_context_;

  // Claim a std::shared_ptr to the fusion buffer to prevent its memory from being reclaimed
  // during finalization.
  auto fusion_buffer = global_state_->fusion_buffer.GetBuffer(
      first_entry.device, first_entry.context->framework(), global_state_->current_nccl_stream);
  gpu_context_->finalizer_thread_pool.execute([entries, first_entry, cpu_buffer, fusion_buffer, free_host_buffer,
                                                evt_queue, &timeline, &gpu_context, error_check_callback]() mutable {
    gpu_context->SetDevice(first_entry.device);

    gpu_context->WaitForEvents(evt_queue, entries, timeline, error_check_callback);
    if (free_host_buffer && cpu_buffer != nullptr) {
      free(cpu_buffer);
    }
    // shutdown allreduce
    for (auto& e : entries) {
      timeline.End(e.tensor_name, e.output);
      // Callback can be null if the rank sent Join request.
      if (e.callback != nullptr) {
        e.callback(Status::OK());
      }
    }
  });
  //if(this->thread_fzh.joinable()) this->thread_fzh.join();
  // Update current stream
  global_state_->current_nccl_stream = (global_state_->current_nccl_stream + 1) %
                                  global_state_->num_nccl_streams;

  return Status::InProgress();
}

GPUAllreduce::GPUAllreduce(GPUContext* context, HorovodGlobalState* global_state)
    : AllreduceOp(global_state), gpu_context_(context), gpu_op_context_(context, global_state) {}

bool GPUAllreduce::Enabled(const ParameterManager& param_manager,
                            const std::vector<TensorTableEntry>& entries,
                            const Response& response) const {
  return entries[0].device != CPU_DEVICE_ID;
}

void GPUAllreduce::MemcpyEntryInFusionBuffer(const std::vector<TensorTableEntry>& entries,
                                             const TensorTableEntry& e, void* buffer_data_at_offset) {
  auto& first_entry = entries[0];
  gpu_context_->MemcpyAsyncD2D(buffer_data_at_offset, e.tensor->data(), (size_t) e.tensor->size(),
                               gpu_context_->streams[global_state_->current_nccl_stream][first_entry.device]);
}

void GPUAllreduce::MemcpyEntryOutFusionBuffer(const std::vector<TensorTableEntry>& entries,
                                               const void* buffer_data_at_offset, TensorTableEntry& e) {
  auto& first_entry = entries[0];
  gpu_context_->MemcpyAsyncD2D((void*) e.output->data(), buffer_data_at_offset, (size_t) e.tensor->size(),
                               gpu_context_->streams[global_state_->current_nccl_stream][first_entry.device]);
}

void GPUAllreduce::ScaleBuffer(double scale_factor, const std::vector<TensorTableEntry>& entries,
                               const void* fused_input_data, void* buffer_data, int64_t num_elements) {
  gpu_context_->ScaleBufferImpl(fused_input_data, buffer_data, num_elements, scale_factor, entries[0].tensor->dtype(),
                                gpu_context_->streams[global_state_->current_nccl_stream][entries[0].device]);

}

GPUAllgather::GPUAllgather(GPUContext* context, HorovodGlobalState* global_state)
    : AllgatherOp(global_state), gpu_context_(context), gpu_op_context_(context, global_state) {}

bool GPUAllgather::Enabled(const ParameterManager& param_manager,
                           const std::vector<TensorTableEntry>& entries,
                           const Response& response) const {
  return entries[0].device != CPU_DEVICE_ID;
}

void GPUAllgather::MemcpyEntryInFusionBuffer(const std::vector<TensorTableEntry>& entries,
                                             const TensorTableEntry& e, void* buffer_data_at_offset) {
  auto& first_entry = entries[0];
  gpu_context_->MemcpyAsyncD2D(buffer_data_at_offset, e.tensor->data(), (size_t) e.tensor->size(),
                               gpu_context_->streams[global_state_->current_nccl_stream][first_entry.device]);
}

void GPUAllgather::MemcpyEntryOutFusionBuffer(const std::vector<TensorTableEntry>& entries,
                                              const void* buffer_data_at_offset, TensorTableEntry& e,
                                              int64_t entry_offset, size_t entry_size) {
  auto& first_entry = entries[0];
  gpu_context_->MemcpyAsyncD2D((int8_t*)e.output->data() + entry_offset, buffer_data_at_offset, entry_size,
                               gpu_context_->streams[global_state_->current_nccl_stream][first_entry.device]);
}

GPUBroadcast::GPUBroadcast(GPUContext* context,
                           HorovodGlobalState* global_state)
    : BroadcastOp(global_state), gpu_context_(context), gpu_op_context_(context, global_state) {}

bool GPUBroadcast::Enabled(const ParameterManager& param_manager,
                           const std::vector<TensorTableEntry>& entries,
                           const Response& response) const {
  return entries[0].device != CPU_DEVICE_ID;
}

GPUAlltoall::GPUAlltoall(GPUContext* context,
		         HorovodGlobalState* global_state)
    : AlltoallOp(global_state), gpu_context_(context), gpu_op_context_(context, global_state) {}

bool GPUAlltoall::Enabled(const ParameterManager& param_manager,
                          const std::vector<TensorTableEntry>& entries,
                          const Response& response) const {
  return entries[0].device != CPU_DEVICE_ID;
}

} // namespace common
} // namespace horovod
