# hip-mpi-testsuite

The hip-mpi-testsuite contains a collection of MPI tests capable of utilizing arbitray combinations of different memory types.
The code requires a ROCm enabled implementation of Open MPI, and UCX compiled with ROCm support.

All tests take the same set of optional arguments:

```
Usage: executable_name -s <sendBufType> -r <recvBufType> -n <elements> -t <sleepTime>
       with sendBufType and recvBufType being :
                  D      Device memory (i.e. hipMalloc) - default if not specified
                  H      Host memory (i.e. malloc)
                  M      Unified memory (i.e hipMallocManaged)
                  O      Device accessible page locked host memory (i.e. hipHostMalloc)
                  R      Registered host memory (i.e. hipHostRegister)
            elements:  number of elements to send/recv
            sleepTime: time in seconds to sleep
```

To compile and run all tests in the testsuite 

```
./configure CXX=mpiCC --with-rocm=/opt/rocm
make
./run_all.sh
```

To compile and run benchmarks execute:

```
make bench
mpirun --mca pml ucx -x UCX_RNDV_THRESH=128 -np 16 ./src/hip_allreduce_bench -s D -r D -n 1048576
```
