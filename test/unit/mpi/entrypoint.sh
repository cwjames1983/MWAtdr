#!/usr/bin/env bash

# Runs the MPI unit tests (with arguments passed through to mpirun if running with Docker).

if [[ "${CONTAINER_RUNTIME,,}" == 'singularity' ]] ; then
    # If we're running in Singularity then the container was already started with srun.
    exec ./build/mpi_unit_test
else
    exec mpirun "$@" ./build/mpi_unit_test
fi
