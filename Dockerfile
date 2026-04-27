FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    nasm \
    gcc \
    gcc-multilib \
    make \
    grub-pc-bin \
    grub-common \
    xorriso \
    mtools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /honeyos
COPY . .

RUN make honeyos.iso

CMD ["bash"]
