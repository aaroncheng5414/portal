ARG BASE_IMAGE=nvcr.io/nvidia/cuda:13.3.0-cudnn-devel-ubuntu24.04
FROM ${BASE_IMAGE} AS opencv

ARG WITH_CUDA=ON
ARG CUDA_ARCH_BIN=8.7
ARG BUILD_JOBS=2

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential ca-certificates cmake curl ninja-build \
       libjpeg-dev libpng-dev libv4l-dev v4l-utils \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /tmp/opencv /tmp/opencv_contrib \
    && curl -fL --retry 3 --retry-all-errors https://codeload.github.com/opencv/opencv/tar.gz/fe38fc608f6acb8b68953438a62305d8318f4fcd -o /tmp/opencv.tar.gz \
    && echo "6a7508554941c1a698c243b2212b2985ce59a65a3e0348d53c2158607d801e61  /tmp/opencv.tar.gz" | sha256sum -c - \
    && tar -xzf /tmp/opencv.tar.gz -C /tmp/opencv --strip-components=1 \
    && curl -fL --retry 3 --retry-all-errors https://codeload.github.com/opencv/opencv_contrib/tar.gz/d99ad2a188210cc35067c2e60076eed7c2442bc3 -o /tmp/opencv_contrib.tar.gz \
    && echo "8d2f5665a4f47fd6763eb75f5017676cbcdeb813074b32a9fda72c25d52abdd4  /tmp/opencv_contrib.tar.gz" | sha256sum -c - \
    && tar -xzf /tmp/opencv_contrib.tar.gz -C /tmp/opencv_contrib --strip-components=1 \
    && curl -fL --retry 3 --retry-all-errors https://raw.githubusercontent.com/opencv/opencv_contrib/f2854f4f5e7b67d4e073ea002ae0174d437e2962/modules/cudev/include/opencv2/cudev/ptr2d/zip.hpp -o /tmp/opencv_contrib/modules/cudev/include/opencv2/cudev/ptr2d/zip.hpp \
    && echo "e6065052850cfb35ef379e4d5c2a153c0efb22713cbd044c54cbf935e02f945b  /tmp/opencv_contrib/modules/cudev/include/opencv2/cudev/ptr2d/zip.hpp" | sha256sum -c - \
    && rm /tmp/opencv.tar.gz /tmp/opencv_contrib.tar.gz

RUN test "${WITH_CUDA}" = ON -o "${WITH_CUDA}" = OFF \
    && cmake -S /tmp/opencv -B /tmp/opencv-build -G Ninja \
       -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_INSTALL_PREFIX=/usr/local \
       -DCMAKE_CXX_STANDARD=17 \
       -DBUILD_LIST=core,imgproc,imgcodecs,videoio,objdetect,dnn,cudev \
       -DOPENCV_EXTRA_MODULES_PATH=/tmp/opencv_contrib/modules/cudev \
       -DWITH_CUDA=${WITH_CUDA} \
       -DWITH_CUDNN=${WITH_CUDA} \
       -DWITH_CUBLAS=${WITH_CUDA} \
       -DOPENCV_DNN_CUDA=${WITH_CUDA} \
       -DCUDA_ARCH_BIN=${CUDA_ARCH_BIN} \
       -DCUDA_ARCH_PTX= \
       -DWITH_NVCUVID=OFF \
       -DWITH_NVCUVENC=OFF \
       -DWITH_CUFFT=OFF \
       -DWITH_FFMPEG=OFF \
       -DWITH_GSTREAMER=OFF \
       -DWITH_V4L=ON \
       -DWITH_OPENCL=OFF \
       -DWITH_IPP=OFF \
       -DWITH_ITT=OFF \
       -DBUILD_TESTS=OFF \
       -DBUILD_PERF_TESTS=OFF \
       -DBUILD_EXAMPLES=OFF \
       -DBUILD_opencv_apps=OFF \
       -DBUILD_opencv_python3=OFF \
       -DBUILD_JAVA=OFF \
    && if [ "${WITH_CUDA}" = ON ]; then \
       grep -q '^#define HAVE_CUDA' /tmp/opencv-build/cvconfig.h \
       && grep -q '^#define HAVE_CUDNN' /tmp/opencv-build/cvconfig.h \
       && grep -q '^OPENCV_DNN_CUDA:BOOL=ON' /tmp/opencv-build/CMakeCache.txt; \
       fi \
    && cmake --build /tmp/opencv-build --parallel ${BUILD_JOBS} \
    && cmake --install /tmp/opencv-build \
    && install -Dm644 /tmp/opencv/LICENSE /usr/local/share/licenses/opencv/LICENSE \
    && install -Dm644 /tmp/opencv_contrib/LICENSE /usr/local/share/licenses/opencv_contrib/LICENSE \
    && ldconfig \
    && rm -rf /tmp/opencv /tmp/opencv_contrib /tmp/opencv-build

FROM opencv AS service

WORKDIR /workspace/cv
COPY CMakeLists.txt ./
COPY include/ ./include/
COPY src/ ./src/
COPY tests/ ./tests/
COPY models/ ./models/
COPY api/ ./api/

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SERVICE=ON \
    && cmake --build build --parallel ${BUILD_JOBS} \
    && ctest --test-dir build --output-on-failure

EXPOSE 8080
CMD ["./build/portal_cv", "--host", "0.0.0.0"]
