title: docker入门
date: 2016-03-28 06:00:11
tags: 程序猿
---

## 背景
从1960s开始算起，整个虚拟化技术的发展超过了五十多年。一上wiki，你会发现虚拟化技术种类繁多，而且各个虚拟化技术的种类界定似乎有时候不是那么好说清楚的。之前在云栖社区上看了一篇直接把我看混了，觉得那篇博客有的地方不是太准确。这里我以wiki为准，只简单的了解了（OS-level Hypervisors）和（Environment-level Containers），因为这里两种虚拟化技术在“云”中都有非常重要的应用。

OS-level Hypervisors以硬件为支撑，需要模拟的是整个操作系统。显然，我们以前使用的vmware workstation就是基于这样的虚拟化技术，为我们提供各式各样的虚拟机。它的竞争者核心竞争者是微软的Hyper-V。以前瞎折腾虚拟机折腾坏了也不会影响宿主机，原因是Hypervisors架在硬件之上，作为各个虚拟操作系统基础设施。据说阿里云和亚马逊的AWS也是基于这样的一个原理构建的云（顺带吐槽他们服务器的命名）。好的，这个不是我们的重点！然后我们就再提另一个不是重点的东西——Microkernel（Hyper-V基于此种内核架构）和Monolithic（vmware workstation基于此种内核架构），这两种内核架构。linus为什么选择了后者而不是前者作为linux的内核。这个涉及到历史上重要的一次讨论（debate），史、称[Tanenbaum–Torvalds debate](https://en.wikipedia.org/wiki/Tanenbaum%E2%80%93Torvalds_debate)。有趣有趣。

Environment-level Containers，称为容器化技术，它以操作系统为支撑，模拟的是运行在某个操作系统上的多个不同进程，将它们封装在一个密闭的容器里面。Docker目前是容器化技术中最出色的项目，貌似Windows Server 2016也实现了对它的原生支持。

亚马逊AWS以及阿里云，它们提供相对廉价的虚拟服务器，一开始受到了多数创业公司的青睐。可以将它们作为生产环境，亦可以作为开发机或者测试机。至少站在运维的角度，这样的云服务器可以减少不少的工作。就阿里云来说，构建大型的网站或者App亦逐渐成熟，无论公有还是私有，各路人马上“云”确实是一个挡不住趋势。

回到正题，第一次接触Docker的人往往会把容器当成是一台主机，除了功能会导致这样的误解外，Docker镜像的名称也是一个很重要的原因OMG。实际上，一个容器是运行在主机上的一个进程，但是它不是普通的进程。我们知道，一个操作系统中，多个普通进程之间是共用CPU，内存，网络等资源的。相对而言，容器是隔离进程的，这些进程运行在一个沙盒中。Docker最初原生只支持linux，借助linux的CGroup和Namespace技术，限制容器使用硬件资源数量和隔离PID、网络等资源。借助Docker我们可以获得什么好处呢？
1、	快速部署和启动，Docker包含了运行环境和可执行程序，秒级别启动速度
2、	方便持续集成和发布
3、	充分利用硬件，多个应用在同一硬件上运行但相互独立
4、	节约成本
但通常来说，对于大型应用来说，不适合直接将Docker作为生产环境。鉴于Docker本身的稳定性，将它作为测试的一个工具是相当不错的，据说Docker+CI也是很令人感到愉快的一种组合（我自己还没有试过，那天试过了再来给分享）。

## 入门：实践

于Docker来说，一个容器中可以运行多个进程，应此，我们甚至可以将一整套的环境都在一个Docker的容器中配置和安装。官方称这类容器为Fat Container。

站在一个运维的角度我（墙裂）不建议这样做，一旦环境复杂起来，若是在最后一步中发现前面装什么紧耦合的组件版本啊库啊什么的不对劲，那种酸爽的感觉...再说了，例如在一个mysql将被多个应用使用的场景中，将mysql部署在任何某个应用中都是不合理的。解耦解耦，写代码的时候要解耦，打环境的时候也要松散，千万不要扎堆。因此，将容器部署为单应用或者单进程是更加被推崇的方式。

在开始之前还有一个概念必须要弄清楚，那就是Docker中image和container之间的区别和关系。
>**image**:中文称为为镜像，一般来说一个镜像是只读的，以一个镜像为蓝本，可以产生多个container（中文称为容器）。镜像的创造是可以叠加的，即可以以一个镜像为蓝本构建一个新的镜像，例如php官方镜像多达13层。
>**container**:一个容器启动之后，会在镜像层上增加一个可写层。容器从启动到停止，它所发生的改变，会写入到这个容器的文件系统层中，成为持久的改变。另外，容器是可以打包（commit）成镜像的。

Dockerfile是什么呢？它由关键字组成，这些关键字都不多，但是功能还是很强的，具体的作用就是告诉docker-deamon怎么去构建你要的镜像。使用Dockerfile创建image是一种比container直接打包更灵活的方式，我们将以Dockerfile为基础构建多个不同Docker镜像，并以镜像为基础运行容器。另外，一般来说呢，我们需要的单进程的一个容器，它的镜像都已经被放在了hub.docker.com（公共仓库）上了。这个网站类似github一样，只不过上面的都是镜像，你需要什么镜像可以去它上面搜索，然后呢，一般镜像的页面会放置一个Dockerfile，这个Dockerfile就是可以直接下载下来使用的，非常的方便。当然除了这个以外，还有一些其他公共仓库，你甚至可以有自己的私有仓库。官方的公共仓库网速真的太慢了，完全受不了啊，在阿里云上拉一个镜像慢的要死要死的。一个镜像几百兆，你看着你那个下载的速度，感觉要疯了。所以，一般你自己可以选择一个在国内比较快的公共仓库。在迫不得已的情况下，只能用已有的基础镜像起容器，然后在容器中搭好环境再打包成image。
先来看一下简单的Dockerfile。
![dockerfile_file_tree](/images/docker_file_tree.jpg)

```
/*mysql 文件中Dockfile文件内容*/
FROM mysql

/*nginx 文件中Dockfile文件内容*/
FROM nginx
RUN  mkdir -p /home/www/htdocs && mkdir -p /home/www/log && mkdir -p /home/www/log/nginx                                                                                                                           
RUN  chown -R www-data.www-data /home/www/htdocs /home/www/log                  
VOLUME ["/home/www"]

/*php 文件中Dockfile文件内容*/
FROM php:5.6                                                                                                                                                                                                       
RUN docker-php-ext-install gd && docker-php-ext-install pdo_mysql
WORKDIR /opt
RUN usermod -u 1000 www-data
VOLUME ["/opt"]
```
上面的关键字其实都很好理解的，比如FROM，就是你要使用的基础镜像名称；RUN就是执行命令；VOLUME是创建一个挂载点用于共享目录。具体的关键字意思看[这里](https://docs.docker.com/engine/reference/builder/)。

切换到myql目录下，运行命令
>docker build -t test/mysql .

好的，此时docker就去读取Dockerfile然后去构建镜像了，完成的速度取决于网速。
假设相关的镜像都下载和构建好了，那么我们就需要考虑怎么把这几个单独的容器关联起来了。
>docker run -p 80:80 -v /home/www:/home/www -it test/nginx

我们通过-p参数，将本地的80端口绑定到容器的80端口，并将本地的/home/www目录挂载到容器的/home/www目录，并且以-it参数进入到容器的控制台。这样做了之后你就会发现，其实测试你已经不需要关心nginx究竟是运行在容器中还是运行在真是的操作系统中了。同理，我们只要将mysql和php按照类似的规则运行起来即可。

更简便的方法是使用docker-compose来把各个容器关联起来。具体的例子也可以去搜一下。本文只是自己理清楚docker究竟是什么，docker的最基本的概念。在真实使用之前，把原理理清楚总是对的，而且可以加速你实践中学习的速度。我再把常用的命令列一下，具体的学习就要在真正的实践中了。
>docker run
>docker image
>docker pull
>docker push
>docker ps
>docker commit
>docker rm
>docker rmi
>docker build
>docker save
>docker load

