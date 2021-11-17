FROM dokken/ubuntu-20.04

    # && npm install n -g \
    # && n stable \

RUN apt-get update \
    && apt-get install net-tools -y \
    && apt-get install git -y \
    && apt-get install npm -y \
    && apt-get install cmake -y \
    && apt-get install gdb -y \
    && apt-get install vim -y \
    && apt-get install software-properties-common -y \
    && add-apt-repository ppa:ubuntu-toolchain-r/ppa -y \
    && apt-get update \
    && apt-get install libtbb-dev -y\
    && apt-get install zlib1g-dev -y \
    && apt-get install libcurl4-gnutls-dev -y \
    && apt-get install supervisor \
    && mkdir /log 
    
EXPOSE 9001:9001

CMD [ "/bin/bash"]

