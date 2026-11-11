ARG BASE_IMAGE=nvcr.io/nvidia/cuda:13.3.0-cudnn-devel-ubuntu24.04
FROM ${BASE_IMAGE}

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace/cv
COPY Makefile ./
COPY include/ ./include/
COPY src/ ./src/
COPY tests/ ./tests/

RUN make test
CMD ["make", "test"]
