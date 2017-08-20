#!/bin/bash
./http_caching_proxy --data_dir htdocs --rest_data rest_data.json --port 8080 --dest localhost:8081 --dest localhost:9090
