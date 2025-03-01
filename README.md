# gkrellm-nvidia

![release](https://img.shields.io/github/v/release/carcass82/gkrellm-nvidia)
![license](https://img.shields.io/github/license/carcass82/gkrellm-nvidia)
![build status](https://github.com/carcass82/gkrellm-nvidia/actions/workflows/build-action.yml/badge.svg)
![code review](https://img.shields.io/codefactor/grade/github/carcass82/gkrellm-nvidia)

### What is it

A simple [GKrellM](http://gkrellm.srcbox.net/) plugin for reading nvidia GPUs data.
GPU and Memory usages, Power Consumption, Clock, Temperature and Fan Speed for multiple GPU are supported.

~~XNVCtrl library *is required*~~
[NVML](https://developer.nvidia.com/nvidia-management-library-nvml) library *is required*

![sample multigpu](doc/screen-dualgpu.jpg)

![sample options](doc/screen-options.jpg)

### Building

- ```make```

### Installation

- ```make install``` (system-wide, defaults to ```/usr/local```)

- ```make install-local``` (home dir)

