#!/bin/bash
#
# A template for creating command line scripts taking options, commands
# and arguments.
#
# Exit values:
#  0 on success
#  1 on failure
#



# Name of the script
SCRIPT=$( basename "$0" )



#
# Message to display for usage and help.
#
function usage
{
    local txt=(
"Roothelper build script"
"Usage: $SCRIPT [options]"
""
"Options:"
"  --help, -h     Print help."
"  --full, -f     Deletes CMake files and runs a full build."
"  --install, -i  Tries to install in /usr/local after build. Needs sudo."
"  --uninstall, -u  Tries to remove installed files from /usr/local. Needs sudo."
    )

    printf "%s\n" "${txt[@]}"
}



#
# Message to display when bad usage.
#
function badUsage
{
    local message="$1"
    local txt=(
"For an overview of the command, execute:"
"$SCRIPT --help"
    )

    [[ $message ]] && printf "$message\n"

    printf "%s\n" "${txt[@]}"
}

function detectcpus
{
    echo "Detecting cpu number"
    cpus=$(nproc --all)
    if [ -z "$cpus" ]; then
        echo "Could not detect cpu number, setting to 2"
        cpus=2
    fi
    echo "cpu number is $cpus"
    export cpus
}


function stdbuild
{
    detectcpus
    set -e

    cd CMAKE
    cmake --build build -- -j$cpus

    cd ..
    cp -f cert/* bin/
}


function fullbuild
{
    detectcpus
    set -e

    cd CMAKE

    mkdir -p ../bin
    rm -rf build

    cmake -H. -Bbuild
    cmake --build build -- -j$cpus

  cd ..
    cp -f cert/* bin/
}

function install
{
  echo "Installing into /usr/local"
    cp bin/r /usr/local/bin/
    cp bin/lib7z.so cert/dummykey.pem cert/dummycrt.pem /usr/local/lib/
}

function uninstall
{
    echo "Uninstalling from /usr/local, if installed"
    rm -f /usr/local/bin/r
    rm -f /usr/local/lib/{lib7z.so,dummykey.pem,dummycrt.pem}
}


if [ $# -eq 0 ]; then
    stdbuild
    exit 0
fi


#
# Process options
#
while (( $# ))
do
    case "$1" in

        --help | -h)
            usage
            exit 0
        ;;

        --uninstall | -u)
            uninstall
            exit 0
        ;;

        --full | -f)
            if [ $# -eq 1 ]; then
                fullbuild
                exit 0
            fi

            case "$2" in
                --install | -i)
                    fullbuild
                    install
                    exit 0
                ;;

                *)
                    badUsage "Option/command not recognized."
                    exit 1
                ;;
            esac

            exit 0
        ;;

        --install | -i)
            if [ $# -eq 1 ]; then
                stdbuild
                install
                exit 0
            fi

            case "$2" in
                --fullbuild | -f)
                    fullbuild
                    install
                    exit 0
                ;;

                *)
                    badUsage "Option/command not recognized."
                    exit 1
                ;;
            esac
            exit 0
        ;;

        *)
            badUsage "Option/command not recognized."
            exit 1
        ;;

    esac
done

badUsage
exit 1
