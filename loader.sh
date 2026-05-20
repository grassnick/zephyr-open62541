#!/bin/bash
# loader.sh - A script to load, execute and debug USoC applications

export PATH=$PATH:/opt/riscv/bin

# debug bridge
export PYTHONPATH=/opt/usoc_debug_bridge/python:$PYTHONPATH
export PULP_CONFIGS_PATH=/opt/usoc_debug_bridge/python
export PATH=/opt/usoc_debug_bridge/bin:$PATH
export LD_LIBRARY_PATH=/opt/usoc_debug_bridge/lib:$LD_LIBRARY_PATH


# test bridge
echo "testing plpbridge command"
plpbridge --chip=usoc_v1 --cable=ftdi read --addr=0x1a110000
exit_code=$?

if [ ${exit_code} -eq 0 ]
then
    echo "plpbridge is executable"
else
    echo "plpbridge is not executable or found"
    exit -1
fi


usage()
{
    echo
    echo "This is a script to load, execute and debug USoC applications."
    echo
    echo "$0 [cmd] [target]"
    echo 
    echo "cmd:    load, exec, run, debug, start, dump"
    echo "target: boot, kernel, user, all, debug_app"
    exit -1
}

get_target()
{
    case "$1" in
        boot) image="usoc_bootloader/build/usoc_bootloader.elf" ;;
        kernel) image="build/usoc_kernel.elf" ;;
        user) image="build/usoc_user.elf" ;;
        test) image="build/usoc_test.elf" ;;
        app) image="build/${2}.elf" ;;
        *) usage ;;
    esac
}

load_all()
{
    plpbridge --chip=usoc_v1 --cable=ftdi --binary usoc_bootloader/build/usoc_bootloader --verbose 10 reset stop load 
    plpbridge --chip=usoc_v1 --cable=ftdi --binary build/usoc_kernel.elf --verbose 10 reset stop load
    plpbridge --chip=usoc_v1 --cable=ftdi --binary build/usoc_user.elf --verbose 10 reset stop load 
}

load()
{
    echo "Load $1 to USoC"
    if [ "$1" = "all" ] ; then
        load_all 
    else
        get_target $1
        plpbridge --chip=usoc_v1 --cable=ftdi --binary $image --verbose 10 reset stop load
    fi
}

execute()
{
    echo "Execute $1 at USoC"
    if [ "$1" = "user" ] || [ "$1" = "test" ]  ; then
        load kernel
    fi 
    get_target $1
    plpbridge --chip=usoc_v1 --cable=ftdi --binary $image --verbose 10 reset stop load reqloop start wait
}

flash_mram()
{
    echo "Debug app [$1] at USoC"
    BUILD_DIR=build
    APP_NAME=$1
    elf_name=${BUILD_DIR}/${APP_NAME}.elf

    if [ -f "${elf_name}" ]; then
        echo "${elf_name} exists."
    else
        echo "${elf_name} does not exist."
        exit -1
    fi

    ./tools/image/plp_write.py -i ${BUILD_DIR}/${APP_NAME}.gnm -j ${BUILD_DIR}/${APP_NAME}.meta.json

}

debug_mram()
{
    echo "Debug app [$1] at USoC"
    BUILD_DIR=build
    APP_NAME=$1
    elf_name=${BUILD_DIR}/${APP_NAME}.elf

    if [ -f "${elf_name}" ]; then
        echo "${elf_name} exists."
    else
        echo "${elf_name} does not exist."
        exit -1
    fi

    ##plpbridge --chip=usoc_v1 --cable=ftdi reset start
    plpbridge --chip=usoc_v1 --cable=ftdi stop

    ./tools/image/plp_write.py -i ${BUILD_DIR}/${APP_NAME}.gnm -j ${BUILD_DIR}/${APP_NAME}.meta.json

    plpbridge --chip=usoc_v1 --cable=ftdi --binary $elf_name gdb start wait --rsp-port=1234
}

debug_sram()
{
    echo "Debug [$1] at USoC"
    BUILD_DIR=cmake-build-debug-zephyr-latest/zephyr
    APP_NAME=$1
    image=${BUILD_DIR}/${APP_NAME}.elf
    if [ -f "${image}" ]; then
        echo "${image} exists."
    else
        echo "${image} does not exist."
        exit -1
    fi
    echo "using image ${image}"
    plpbridge --chip=usoc_v1 --cable=ftdi stop reset
    plpbridge --chip=usoc_v1 --cable=ftdi --binary $image load
    sleep 3
    #plpbridge --chip=usoc_v1 --cable=ftdi --binary $image reset stop load start reset
    plpbridge --chip=usoc_v1 --cable=ftdi gdb reset start wait --rsp-port=1234
}


run()
{
    echo "Run $1 at USoC"
    if [ "$1" = "user" ] ; then
        load kernel
    fi
    get_target $1
    plpbridge --chip=usoc_v1 --cable=ftdi --binary $image reset stop load reqloop start gdb wait --rsp-port=1234
}

debug_app()
{
    echo "Debug App $1 at USoC"
    get_target $1
    plpbridge --chip=usoc_v1 --cable=ftdi --binary $image reset stop load reqloop start gdb wait --rsp-port=1234
}

dump()
{
    echo "Create dump for $1"
    get_target $1
    riscv32-unknown-elf-objdump -dr --source $image > dump_$1.txt
}

start()
{
    echo "Restart usoc"
    plpbridge --chip=usoc_v1 --cable=ftdi reset stop start
}

if [ -z $1 ] ; then
    usage
fi

if [ $1 == "start" ] ; then
    start
    exit
fi

if [ -z $2 ] ; then
    usage
fi

image=""
case "$1" in
    load) load $2 ;;
    exec) execute $2 ;;
    flash_mram) flash_mram $2;;
    debug_mram) debug_mram $2;;
    debug_sram) debug_sram $2;;
    run) run $2 ;;
    dump) dump $2 ;;
    *) usage ;;
esac