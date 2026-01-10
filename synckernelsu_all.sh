#!/bin/bash

if [ -d KernelSU ]; then
    echo "Found KernelSU Folder, removing it..."
    rm -rf KernelSU
	rm -rf drivers/kernelsu
else
    echo "KernelSU Folder not found, proceeding..."
fi

read -p "Please enter the version number (A/a is KSU-Next Latest release.B/b is KSU-Next Stable branch.C/c is Legacy branch.: " version

if [ "$version" == "" ]; then
    echo "No version specified. Exiting."
    exit 1
fi

case $version in
    [Aa]*)
        curl -LSs "https://raw.githubusercontent.com/KernelSU-Next/KernelSU-Next/next/kernel/setup.sh" | bash -
        ;;
    [Bb]*)
        curl -LSs "https://raw.githubusercontent.com/KernelSU-Next/KernelSU-Next/next/kernel/setup.sh" | bash -s stable
        ;;
    [Cc]*)
        curl -LSs "https://raw.githubusercontent.com/KernelSU-Next/KernelSU-Next/next/kernel/setup.sh" | bash -s legacy
        ;;
    *)
        echo "Invalid option. Exiting."
        exit 1
        ;;
esac
