HOROVOD_FUSION_THRESHOLD=2147483648 NCCL_MIN_NCHANNELS=8 FUSION_SIZE=53,53,52 FUSION_BLOCK_NUM=1,1,1 FUSION_THREAD_NUM=64,64,64  HOROVOD_STREAM_ASSIGNMENT=2,3,4 HOROVOD_TIMELINE=/home/users/fzh/log/MobileNetV2_b16_t64_g1 NCCL_MAX_NCHANNELS=8 NCCL_ALGO=ring horovodrun --verbose --gloo --log-level INFO --network-interface "lo" -np 2 -H localhost:2 python3 /home/users/fzh/horovod-0.20.3/examples/tensorflow2/tensorflow2_synthetic_benchmark.py --model MobileNetV2 --batch-size 16 --num-iters 5