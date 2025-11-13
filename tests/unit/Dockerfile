FROM ubuntu:jammy
ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update
RUN apt-get install --no-install-recommends -y apt-transport-https
RUN apt-get update
RUN apt-get install --no-install-recommends -y curl gnupg2 ca-certificates software-properties-common nlohmann-json3-dev

RUN apt-get update && apt-get install --no-install-recommends -y git cmake build-essential sqlite3 libsqlite3-dev libssl-dev librdkafka-dev libboost-all-dev libtool libxerces-c-dev libflatbuffers-dev libjsoncpp-dev libspdlog-dev pigz libcurl4-openssl-dev uncrustify libyaml-cpp-dev libprotobuf-dev protobuf-compiler libxml2-dev libkrb5-dev uuid-dev libgsasl7-dev libgrpc++-dev libgrpc-dev pkg-config libc-ares-dev libre2-dev libabsl-dev  libopenblas-dev libomp-dev libgflags-dev && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN add-apt-repository ppa:deadsnakes/ppa
RUN apt-get install --no-install-recommends -y python3.8-dev python3-pip python3.8-distutils
RUN python3.8 -m pip install stellargraph
RUN python3.8 -m pip install chardet scikit-learn joblib threadpoolctl pandas
RUN python3.8 -m pip cache purge

# Build and install OpenTelemetry C++ SDK with exporters
WORKDIR /tmp
RUN git clone --branch v1.16.1 --recurse-submodules https://github.com/open-telemetry/opentelemetry-cpp.git
WORKDIR /tmp/opentelemetry-cpp
RUN mkdir build && cd build && \
    cmake -DWITH_PROMETHEUS=ON \
          -DWITH_OTLP_GRPC=OFF \
          -DWITH_OTLP_HTTP=ON \
          -DBUILD_TESTING=OFF \
          -DWITH_EXAMPLES=OFF \
          -DCMAKE_INSTALL_PREFIX=/usr/local \
          .. && \
    make -j$(nproc) && \
    make install && \
    ldconfig \
    && cd / && rm -rf /tmp/opentelemetry-cpp

# Set environment variables to help CMake find OpenTelemetry
ENV PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"
ENV CMAKE_PREFIX_PATH="/usr/local:$CMAKE_PREFIX_PATH"

RUN curl -fsSL https://download.docker.com/linux/debian/gpg | apt-key add -
RUN add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"
RUN apt-get update
RUN apt-get install --no-install-recommends -y docker-ce-cli
RUN rm -f /var/lib/apt/lists/* || echo "Some files were not deleted"

WORKDIR /home/ubuntu
RUN mkdir software
WORKDIR /home/ubuntu/software

RUN git clone --single-branch --depth 1 --branch v5.1.1-DistDGL-v0.5 https://github.com/KarypisLab/METIS.git
RUN git clone --single-branch --depth 1 --branch v0.4.1 https://github.com/mfontanini/cppkafka.git
RUN git clone --single-branch --depth 1  --branch v0.11.0  https://github.com/kubernetes-client/c
RUN git clone --single-branch --depth 1 --branch v4.2-stable https://libwebsockets.org/repo/libwebsockets
RUN git clone --single-branch --depth 1 --branch release/0.2.5 https://github.com/yaml/libyaml
RUN git clone --single-branch --depth 1 --branch v4.11.1 https://github.com/antlr/antlr4.git
RUN git clone --single-branch --depth 1 https://github.com/miyurud/libhdfs3.git

WORKDIR /home/ubuntu/software/METIS
RUN git submodule update --init
RUN find . -type f -print0 | xargs -0 sed -i '/-march=native/d'
RUN make config shared=1 cc=gcc prefix=/usr/local
RUN make install



RUN mkdir /home/ubuntu/software/cppkafka/build
WORKDIR /home/ubuntu/software/cppkafka/build
RUN cmake ..
RUN make -j4
RUN make install

RUN mkdir /home/ubuntu/software/libwebsockets/build
WORKDIR /home/ubuntu/software/libwebsockets/build
RUN cmake -DLWS_WITHOUT_TESTAPPS=ON -DLWS_WITHOUT_TEST_SERVER=ON -DLWS_WITHOUT_TEST_SERVER_EXTPOLL=ON \
      -DLWS_WITHOUT_TEST_PING=ON -DLWS_WITHOUT_TEST_CLIENT=ON -DCMAKE_C_FLAGS="-fpic" -DCMAKE_INSTALL_PREFIX=/usr/local ..
RUN make
RUN make install

RUN mkdir /home/ubuntu/software/libyaml/build
WORKDIR /home/ubuntu/software/libyaml/build
RUN cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_TESTING=OFF  -DBUILD_SHARED_LIBS=ON ..
RUN make
RUN make install

RUN mkdir /home/ubuntu/software/c/kubernetes/build
WORKDIR /home/ubuntu/software/c/kubernetes/build
RUN cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
RUN make
RUN make install

WORKDIR /home/ubuntu/software
WORKDIR /home/ubuntu/software/antlr4/runtime/Cpp
RUN mkdir /home/ubuntu/software/antlr4/runtime/Cpp/build
WORKDIR /home/ubuntu/software/antlr4/runtime/Cpp/build
RUN cmake ..
RUN make install

RUN mkdir /home/ubuntu/software/libhdfs3/build
WORKDIR /home/ubuntu/software/libhdfs3/build
RUN ../bootstrap --prefix=/usr/local/libhdfs3
RUN make -j8
RUN make install

RUN curl -L https://github.com/Kitware/CMake/releases/download/v3.29.6/cmake-3.29.6.tar.gz \
    -o cmake-3.29.6.tar.gz \
 && tar -zxvf cmake-3.29.6.tar.gz \
 && cd cmake-3.29.6 \
 && ./bootstrap \
 && make -j$(nproc) \
 && make install \
 && cd .. \
 && rm -rf cmake-3.29.6 cmake-3.29.6.tar.gz

WORKDIR /usr/local/lib
RUN apt-get update && apt-get install --no-install-recommends -y git  libopenblas-dev libomp-dev libgflags-dev
RUN git clone --depth=1 https://github.com/facebookresearch/faiss.git
WORKDIR /usr/local/lib/faiss
RUN mkdir build && cd build \
 && cmake -DFAISS_ENABLE_PYTHON=OFF -DBUILD_TESTING=OFF -DFAISS_ENABLE_GPU=OFF .. \
 && make -j$(nproc) \
 && make install

 RUN apt-get purge -y --autoremove git
RUN rm -rf /home/ubuntu/software/*

WORKDIR /home/ubuntu/software/code
RUN apt-get update && apt-get install --no-install-recommends -y default-jre
RUN curl -O https://s3.amazonaws.com/artifacts.opencypher.org/M23/Cypher.g4
RUN curl -O https://www.antlr.org/download/antlr-4.13.2-complete.jar
RUN java -jar antlr-4.13.2-complete.jar -Dlanguage=Cpp -visitor Cypher.g4
RUN apt-get purge default-jre -y

WORKDIR /home/ubuntu/software
RUN mkdir /home/ubuntu/software/antlr
RUN mv /home/ubuntu/software/code/*.cpp /home/ubuntu/software/antlr
RUN mv /home/ubuntu/software/code/*.h /home/ubuntu/software/antlr
RUN rm -rf /home/ubuntu/software/code

RUN apt-get update
RUN apt-get install libcurl4-openssl-dev -y
RUN apt-get install -y sysstat
RUN apt-get install -y nmon

RUN ln -sf /usr/bin/python3.8 /usr/bin/python3

RUN python3 -m pip install --no-cache-dir gcovr==6.0

ENV TERM=xterm-256color
ENV HOME="/home/ubuntu"
ENV JASMINEGRAPH_HOME="${HOME}/software/jasminegraph"

WORKDIR "${JASMINEGRAPH_HOME}"

COPY ./cmake_modules ./cmake_modules
COPY ./build.sh ./build.sh
COPY ./CMakeLists.txt ./CMakeLists.txt
COPY ./src_python ./src_python
COPY ./src ./src
COPY ./ddl ./ddl
COPY ./main.h ./main.h
COPY ./main.cpp ./main.cpp
COPY ./globals.h ./globals.h
COPY ./conf ./conf
COPY ./k8s ./k8s

RUN mkdir build
RUN mkdir tests
RUN mkdir tests/unit
COPY ./tests/unit ./tests/unit

WORKDIR "${JASMINEGRAPH_HOME}/build"

RUN cmake -DCMAKE_BUILD_TYPE=DEBUG ..
RUN make -j20

CMD ["bash", "-c", "LD_LIBRARY_PATH=/usr/local/lib make -j1 coverage && cp coverage.xml ../coverage/"]
