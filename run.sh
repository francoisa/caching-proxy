#!/bin/bash
LD_LIBRARY_PATH=/bb/blaw/tools/gcc-4_8_0/4.8.0/lib64
LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:/bb/blaw/tools/boost-1_52_0/4.8.0/lib

export LD_LIBRARY_PATH
./http_caching_proxy --data_dir htdocs \
    --rest_data rest_data.json --port 8080 \
    --dest vsearch-alpha.bdns.bloomberg.com:7777 --debug
#gdb --silent --args http_caching_proxy --data_dir htdocs \
#   --rest_data rest_data.json --port 8080 \
#   --dest vsearch-alpha.bdns.bloomberg.com:7777 --debug
