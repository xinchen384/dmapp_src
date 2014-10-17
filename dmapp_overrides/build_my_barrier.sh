set -ex
export NWCHEM_TOP=/home/xinchen/installed/nwchem-6.3/ 
export NWCHEM_TARGET=LINUX64
export NWCHEM_MODULES=all
export USE_MPI=y
export USE_MPIF=y
export USE_MPIF4=y
export USE_SCALAPACK=y
export USE_64TO32=y
export LIBMPI=" "
module swap PrgEnv-intel PrgEnv-gnu
module load craype-hugepages64M
#module load gcc/4.7.2  
export ARMCI_NETWORK=mpi
export USE_SCALAPACK=y
export SCALAPACK=-lsci_gnu
export SCALAPACK_LIB=-lsci_gnu
export BLASOPT=-lsci_gnu
export BLAS_LIB=-lsci_gnu
export LAPACK_LIB=-lsci_gnu
export USE_64TO32=y
export CFLAGS_FORGA="-g -fno-omit-frame-pointer"
export FFLAGS_FORGA="-g -fno-omit-frame-pointer"
#make realclean
#make nwchem_config
#make 64_to_32
#make FC=/global/homes/m/mc29/myCompilerWrapper/myFC.sh CC=/global/homes/m/mc29/myCompilerWrappers/myCC.sh   >& make.log
#make FC=ftn  >& make.log
#/home/xinchen/installed/libunwind

ftn -fdefault-integer-8 -Wextra -Wuninitialized  -g -O   -I.  -I $NWCHEM_TOP/src/include -I $NWCHEM_TOP/src/tools/install/include -DCRAYXT -DEAFHACK -DNOFSCHECK -DEXT_INT -DLINUX -DLINUX64 -DCHKUNDFLW -DGCC4 -DGCC46 -DSCALAPACK -DPARALLEL_DIAG -DCOMPILATION_DATE="'`date +%a_%b_%d_%H:%M:%S_%Y`'" -DCOMPILATION_DIR="'/home/xinchen/installed/nwchem-6.3/'" -DNWCHEM_BRANCH="'6.3'"  -c -o nwchem.o nwchem.F

ftn -fdefault-integer-8 -Wextra -Wuninitialized  -g -O   -I.  -I $NWCHEM_TOP/src/include -I $NWCHEM_TOP/src/tools/install/include -DCRAYXT -DEAFHACK -DNOFSCHECK -DEXT_INT -DLINUX -DLINUX64 -DCHKUNDFLW -DGCC4 -DGCC46 -DSCALAPACK -DPARALLEL_DIAG -DCOMPILATION_DATE="'`date +%a_%b_%d_%H:%M:%S_%Y`'" -DCOMPILATION_DIR="'/home/xinchen/installed/nwchem-6.3/'" -DNWCHEM_BRANCH="'6.3'"  -c -o stubs.o stubs.F

ftn   -L $NWCHEM_TOP/lib/LINUX64 -L $NWCHEM_TOP/src/tools/install/lib  -o $NWCHEM_TOP/bin/LINUX64/nwchem_bd nwchem.o stubs.o -lnwctask -lccsd -lmcscf -lselci -lmp2 -lmoints -lstepper -ldriver -loptim -lnwdft -lgradients -lcphf -lesp -lddscf -ldangchang -lguess -lhessian -lvib -lnwcutil -lrimp2 -lproperty -lnwints -lprepar -lnwmd -lnwpw -lofpw -lpaw -lpspw -lband -lnwpwlib -lnwxc -lcafe -lspace -lanalyze -lqhop -lpfft -ldplot -ldrdy -lvscf -lqmmm -lqmd -letrans -lpspw -ltce -lbq -lcons -lperfm -ldntmc -lccca -lnwcutil -lga -larmci -lpeigs -lperfm -lcons -lbq -lnwcutil -lsci_gnu   -l64to32 -lsci_gnu     -Wl,--wrap=MPI_Init  -Wl,--wrap=MPI_Barrier /home/xin/dynamic/dmapp_overrides/my_barrier.o  -L/home/xin/dynamic/installed/libunwind/lib/ -lunwind -lstdc++ 

