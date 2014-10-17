set -ex
CFLAGS=-O3
GOOGLE_SPARSE_HASH_PATH=/home/xinchen/installed/sparsehash
LIB_UNWIND_PATH=/home/xinchen/installed/libunwind
NWCHEM_TOP=/home/xinchen/installed/nwchem-6.3
#/opt/openmpi-1.6-gnu/
g++ -c -g $CFLAGS my_barrier.cpp -o my_barrier.o -I /opt/openmpi-1.6-gnu/include -L /opt/openmpi-1.6-gnu/lib/ -I$GOOGLE_SPARSE_HASH_PATH/include -I$LIB_UNWIND_PATH/include -I$NWCHEM_TOP/src/tools/install/include/ 

#-fno-omit-frame-pointer

#gcc -E -B -c test.c -o dmapp_wrapped.E -I/opt/cray/dmapp/7.0.1-1.0501.8315.8.4.ari/include  -I/opt/cray/gni-headers/3.0-1.0501.8317.12.1.ari/include
#gcc -E -B -c test.c -o dmapp_wrapped.E 
#python dmapp-wrapper.py dmapp_wrapped.E  dmapp_wrapped.c  wrap_scrpt
#gcc -c -g $CFLAGS dmapp_wrapped.c -o dmapp_wrapped.o 
#cd /global/homes/w/wlav/milind/nwchem-6.3/src/
#sh build_barrier_deletion.sh
#cd -
