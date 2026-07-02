# Installation Guide<a name="EN-US_TOPIC_0000002552472695"></a>

## Feature Description<a name="EN-US_TOPIC_0000002551640122"></a>

The Ucache smart read cache uses smart I/O prefetch to accurately identify hotspot requests, prefetch I/Os of the sequential pattern, interval pattern and more, and load I/Os to the read cache in advance. In addition, it uses the LRU algorithm to evict cold data, improving the I/O hit ratio and read performance.

## Environment Requirements<a name="EN-US_TOPIC_0000002551640121"></a>

This document provides guidance based on the Kunpeng server and openEuler OS. Before performing operations, ensure that your hardware and software meet the requirements.

**Hardware Requirements<a name="en-us_topic_0000001217080138_section10273165810425"></a>**

| Item | Description |
|-------|------------|
| CPU | Kunpeng 920|

**Software Requirements<a name="section1240364411598"></a>**

| Item | Description            |
|------|------------------------|
| OS  | <ul><li>openEuler 20.03 LTS SP1</li><li>openEuler 22.03 LTS SP1</li></ul>  |
| Ucache | 1.0.0                |

>![](public_sys-resources/icon-note.gif) **NOTE**
>
>Ucache is based on open-source OCF. You can obtain the source code from [here](https://gitcode.com/boostkit/ocf/tree/dev-UCache).

This document provides guidance based on the Kunpeng server and openEuler OS. Before performing operations, ensure that your hardware and software meet the requirements.

## Compiling and Installing the Read Cache Library<a name="EN-US_TOPIC_0000002520640142"></a>

**Procedure<a name="section16195192562111"></a>**

1. Download the OCF repository code, apply the UCache patch, and package the code.

    ```sh
    cd /home
    git clone https://github.com/Open-CAS/ocf.git
    cd ocf
    git checkout d1d6d7cb5f55b616d2aa5123f84ce4ece10fdb0b
    wget https://gitcode.com/boostkit/ocf/blob/master/ucache.patch
    git apply ucache.patch
    cd ..
    tar -zcvf lava-ocf-adaptor-1.0.0.tar.gz ocf/
    ```

2. Go to the `/home` directory and create an RPM build directory.

   1. Install the rpmbuild tool in the environment.

        ```sh
        yum install rpm-build
        ```

   2. Modify the `.rpmmacros` file.

        ```sh
        vi /root/.rpmmacros
        ```

   3. Change the value of `%_topdir` to `/home/rpmbuild`. If the file does not contain this line, add it, save the file, and exit.

        ![](figures/zh-cn_image_0000002520640162.png)

   4. Run the `rpmbuild` installation command again.

        ```sh
        rpmdev-setuptree
        ```

3. Modify the **rpmmacros** file and comment out the content in the red box below.

    ```sh
    vi /root/.rpmmacros
    ```

    ![](figures/zh-cn_image_0000002520480174.png)

4. Copy the source code package and spec file to the `/home/rpmbuild` subdirectory.

    ```sh
    cp /home/lava-ocf-adaptor-1.0.0.tar.gz /home/rpmbuild/SOURCES
    cp /home/ocf/lava-ocf-adaptor.spec /home/rpmbuild/SPECS
    ```

5. Compile the RPM package.

    Default package build command:

    ```sh
    rpmbuild -bb /home/rpmbuild/SPECS/lava-ocf-adaptor.spec
    ```

    After the compilation is complete, the following RPM package is generated:

    ![](figures/zh-cn_image_0000002520640158.png)

6. Install the RPM package.

    ```sh
    cd /home/rpmbuild/RPMS/aarch64/
    rpm -ivh lava-ocf-adaptor-1.0.0-1.aarch64.rpm
    ```

    After the installation, the **lava-ocf-adaptor-1.0.0-1.aarch64.rpm** file is as follows:

    ![](figures/zh-cn_image_0000002551520153.png)

    >![](public_sys-resources/icon-note.gif) **NOTE**
    >
    >`ocf_adaptor.h` describes all external interfaces. Other header files define structures and error codes.
    >To integrate the read cache into an application, add the link option `-llava_cache` during compilation.

## Change History

| Date  | Description       |
|-------|----------|
| 2024-06-30 | This is the first official release. |
