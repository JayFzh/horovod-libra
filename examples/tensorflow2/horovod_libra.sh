pip uninstall -y horovod
cd /home/users/fzh/horovod-libra
rm -rf build
rm -rf dist
rm -rf horovod.egg-info
HOROVOD_GPU_ALLREDUCE=NCCL HOROVOD_GPU_BROADCAST=NCCL HOROVOD_WITHOUT_MXNET=1 HOROVOD_WITHOUT_PYTORCH=1 NCCL_ROOT=/home/users/fzh/nccl-libra/build python3 setup.py sdist
HOROVOD_GPU_ALLREDUCE=NCCL HOROVOD_GPU_BROADCAST=NCCL HOROVOD_WITHOUT_MXNET=1 HOROVOD_WITHOUT_PYTORCH=1 NCCL_ROOT=/home/users/fzh/nccl-libra/build pip3 install ./dist/horovod-0.20.3.tar.gz
