#!/bin/bash
docker run -it --rm \
  -v $(pwd):/workspace \
  -w /workspace \
  -p 8080:8080 \
  gcc:latest bash
