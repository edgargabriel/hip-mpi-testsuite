/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/******************************************************************************
 * Copyright (c) 2024 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *****************************************************************************/

#include <stdio.h>
#include "mpi.h"

#include <hip/hip_runtime.h>

#include "hip_mpitest_utils.h"
#include "hip_mpitest_buffer.h"

int elements = 1024;
hip_mpitest_buffer *sendbuf = NULL;
hip_mpitest_buffer *recvbuf = NULL;

static void init_sendbuf(int *sendbuf, int count, int val)
{
    for (int i = 0; i < count; i++) {
        sendbuf[i] = val;
    }
}

static void init_recvbuf(int *recvbuf, int count)
{
    for (int i = 0; i < count; i++) {
        recvbuf[i] = 0;
    }
}

static bool check_recvbuf(int *recvbuf, int result, int count)
{
    bool res = true;

    for (int i = 0; i < count; i++) {
        if (recvbuf[i] != result) {
            res = false;
#ifdef VERBOSE
            printf("recvbuf[%d] = %d expected %d\n", i, recvbuf[i], result);
#endif
            break;
        }
    }
    return res;
}

int type_p2p_bl_mult_test(int *sendbuf, int *recvbuf, int count, MPI_Comm comm);

/* High level idea of this test is to stress sycnhronization between of buffers
** between Sender and Receiver.
**  - Sender has a single send buffer that is used across all iterations
**  - In every iteration Sender changes the Send buffer
**  - Receiver has a separate receive buffer for every iteration
**  - Receive buffers are checked for correctnes after all communications
**    are done.
*/
int main(int argc, char *argv[])
{
    int rank, nProcs;
    int root = 0;
    int nIter = 10;
    int ret;

    bind_device();

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &nProcs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (nProcs != 2) {
        printf("This test requires exactly two processes!\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    parse_args(argc, argv, MPI_COMM_WORLD);

    int *tmp_sendbuf = NULL, *tmp_recvbuf = NULL;
    // Initialise send buffer
    if (rank == 0) {
        ALLOCATE_SENDBUFFER(sendbuf, tmp_sendbuf, int, elements, sizeof(int),
                            rank, MPI_COMM_WORLD, init_sendbuf, out);
    }

    // Initialize recv buffer
    if (rank == 1) {
        ALLOCATE_RECVBUFFER(recvbuf, tmp_recvbuf, int, nIter *elements, sizeof(int),
                            rank, MPI_COMM_WORLD, init_recvbuf, out);
    }

    // execute point-to-point operations
    for (int i=0; i<nIter; i++)  {
        int *rbuf = NULL;
        int *sbuf = NULL;

        if (rank == 0) {
            if (sendbuf->NeedsStagingBuffer()) {
                init_sendbuf(tmp_sendbuf, elements, i+1);
                HIP_CHECK(sendbuf->CopyTo(tmp_sendbuf, elements * sizeof(int)));
                sbuf = (int *)sendbuf->get_buffer();
            }
            else {
                sbuf = (int *)sendbuf->get_buffer();
                init_sendbuf(sbuf, elements, i+1);
            }
        } else if (rank == 1){
            rbuf = (int *)recvbuf->get_buffer() + i*elements;
        }

        ret = type_p2p_bl_mult_test(sbuf, rbuf, elements, MPI_COMM_WORLD);
        if (MPI_SUCCESS != ret) {
            printf("Error in type_p2p_bl_mult_test. Aborting\n");
            goto out;
        }
    }

    // verify results
    bool res, fret;
    res = true;
    if (rank == 1) {
        int *rbuf_base = (int *)recvbuf->get_buffer();
        if (recvbuf->NeedsStagingBuffer()) {
            HIP_CHECK(recvbuf->CopyFrom(tmp_recvbuf, nIter * elements * sizeof(int)));
            rbuf_base = tmp_recvbuf;
        }
        for (int i = 0; i <nIter; i++) {
            int *rbuf = rbuf_base + i * elements;
            res = check_recvbuf(rbuf, i+1, elements);
            if (res != true) {
                break;
            }
        }
    }
    fret = report_testresult(argv[0], MPI_COMM_WORLD, sendbuf->get_memchar(), recvbuf->get_memchar(), res);

 out:
    // Cleanup dynamic buffers
    if (rank == 0) {
        FREE_BUFFER(sendbuf, tmp_sendbuf);
        delete (sendbuf);
    }
    if (rank == 1) {
        FREE_BUFFER(recvbuf, tmp_recvbuf);
        delete (recvbuf);
    }
    if (MPI_SUCCESS != ret) {
        MPI_Abort (MPI_COMM_WORLD, 1);
        return 1;
    }
    
    MPI_Finalize();
    return fret ? 0 : 1;
}

int type_p2p_bl_mult_test(int *sbuf, int *rbuf, int count, MPI_Comm comm)
{
    int size, rank, ret;
    int tag = 251;
    MPI_Status status;

    MPI_Comm_size(comm, &size);
    MPI_Comm_rank(comm, &rank);

    if (rank == 0) {
        ret = MPI_Ssend(sbuf, count, MPI_INT, 1, tag, comm);
        if (MPI_SUCCESS != ret) {
            return ret;
        }
    }
    if (rank == 1) {
        ret = MPI_Recv(rbuf, count, MPI_INT, 0, tag, comm, &status);
        if (MPI_SUCCESS != ret) {
            return ret;
        }
    }

    return MPI_SUCCESS;
}
