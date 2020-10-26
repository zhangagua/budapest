---
title: 从Hyper混合支持OLAP与OLTP谈起，到网络通信库的架构，再到fork
date: 2016-07-24 10:04:39
tags: 数据库
categories: 数据库
---

这段时间一直在忙着准备临近的秋招，看的东西也少了，加之女票的出国问题困扰，导致很久没更新了。再看陈硕那本《linux多线程服务端编程》时，感觉比之前看的时候通透多了，很多东西在有过实践经验之后，再回过头来，会有醍醐灌顶的感觉（包括对paxos协议的理解也是）。在看到关于多线程fork的时候，再回头想起hyper采用了fork这种方式来实现OLAP与OLTP的混合，就决定写下这一篇。

# 背景

在线联机事务处理（OLTP）和在线分析处理（OLAP）这两个领域对于数据库架构存在不同的挑战。一般说来OLTP与OLAP都是分离的，我们用一个将OLTP数据库来承担事务处理，然后再定期通过ETL（Extract-Transform-Load）工具转移到OLAP数据仓库中来进行分析。

随着商业智能的重要性越来越凸显，这种OLTP与OLAP分离的架构的缺点凸显了出来：
>过期数据：由于ETL处理只能周期性的执行，显然数据必然不是最新的。
冗余：两个系统，整个数据都是两份
维护：两个系统都得维护，开支两倍

于是，我们可以看到类似的SPA HANA便是在尝试整合OLTP与OLAP。而Hyper作为一个单机数据库，也进行了这样的尝试。

# Hyper

文章在这里[《HyPer - Hybrid OLTP&OLAP High Performance Database System》](http://www.hyper-db.de/HyperTechReport.pdf)

有兴趣的同学点进去看一下，我这里为了简要就不一字一句的来讲了。简明扼要的贴几张图，然后附上几个关键点。


在一次read请求中，hyper fork了一次自己，此时两个进程中用使用物理页是完全一模一样的
![hyper_fork](/images/hyper_olap_oltp_1.png)

假设主进程此时数据更新了，那么操作系统会开辟新的物理页（拷贝自旧的物理页），然后写数据，当然之前fork出来的用于读的进程任然是指向旧的物理页。读进程看到的数据，永远是fork一瞬间的那个样子。
![hyper_copy_on_write](/images/hyper_olap_oltp_2.png)

于是这个为了读而产生的进程，则可以根据策略，决定服务OLAP的时长或者次数（若这个进程服务太久，那么势必会出现OLAP使用大量过期数据的现象，所以一个为OLAP产生的进程生命周期是有限的）。后面我们都称这种被fork出来的进程为OLAP进程。
![hyper_snapshot_for_query](/images/hyper_olap_oltp_3.png)

hyper在不同时期产生的OLAP进程，看到的数据是不已一致的。鉴于OLAP的频率一般很低，我觉得没必要存在多个OLAP进程。
![hyper_muti_query](/images/hyper_olap_oltp_4.png)

此外，数据库该有的可靠性保证和检查点，hyper也提到了
![hyper_redolog_storage](/images/hyper_olap_oltp_5.png)

OLTP进程里面还进行了数据分区，每个线程服务一个分区（跨分区事务没提，不过我猜也是有单独的线程负责，该加锁就加锁）。
![hyper_muti_thread](/images/hyper_olap_oltp_6.png)

有没有觉得这样就实现了OLAP和OLTP的混合很神奇很简单？没有复杂的公式没有负责的逻辑控制，我们需要的仅仅是一个简简单单的fork！how amazing！（这是我当时看完之后的感觉，请不要说笑我，毕竟我是小菜）
这篇文章还是很良心的，讲的比较清晰和浅显。不过落到实际，要想清楚代码怎么写还是挺费神的。
因为逻辑地址与物理地址的映射这个事儿是操作系统在管，而且硬件设施有一定的优化，所以其实hyper把最核心的工作交给了我们的操作系统和硬件了。典型的甩锅。文章还有个点，就是一直没有提及fork的效率以及OLTP的测试，不知道这样子fork的效率到底是怎么样的。

# 神奇的fork

那么为什么我之前就看了这篇paper但是一直没有下笔写呢，因为没有触动点。那为什么我现在又写了呢？肯定是有触动点了撒（我真是个2货，这种废话也写）。
事情是这样的，还是陈硕的书引起来的。
网络通信库的框架也就那么几种。
>a:一个主进程，来一个服务启动一个进程；
b:来一个服务，启动一个线程；
c:用一个线程loop来事件，来多个服务都在这个loop里面处理，后面跟线程池，重活交给线程池；
d:多个线程/进程都含有一个loop，每次来事件各自在各自的线程/进程里面处理；

其中a，b两种方式以前貌似当过主流的方式，很多人用的。就我的编程经验来看，对于linux来说，如果你用系统调用产生的线程，那这个线程可不太轻；而linux上jvm上跑的java线程，则是比较轻的。正是陈硕在这里谈架构的时候，我开始思考线程的问题、fork的问题。

## 理清楚进程、轻量级进程、线程的区别，还有内核线程与用户线程的区别

之前我和同学讨论过内核线程和用户线程，但是后来发现这个问题真的很难很难很难理解，而且很难很难很难解释清楚，这个疑问一直挂在心上，虽然当时感觉是谈论出了一个结果，可是还是保有疑问的。

[Light-Weight Processes: Dissecting Linux Threads](http://opensourceforu.com/2011/08/light-weight-processes-dissecting-linux-threads/)

[What are Linux Processes, Threads, Light Weight Processes, and Process State](http://www.thegeekstuff.com/2013/11/linux-process-and-threads/?utm_source=feedburner&utm_medium=feed&utm_campaign=Feed%3A+TheGeekStuff+(The+Geek+Stuff))

[fork,vfork,clone的区别](http://stackoverflow.com/questions/4856255/the-difference-between-fork-vfork-exec-and-clone)

读了上面两个文章后，大致知道linux下通过pthread_create启起来的是线程，clone启起来的是LWP。我之前的疑惑是linux的通过系统调用启动起来的线程确实是和jvm上的线程不一样。pthread_create启动起来的线程似乎很像内核线程，但是它也属于用户线程。于是逐步的去了解fork、vfork、clone这三个系统调用的区别，顺便了解exec。最后，知道了kenel_thread这个系统提供的api就会明白这才是真正内核线程。

## 你所不知道的fork

你试过在多线程的情况下进行fork么？前面的Hyper本身自己是多线程的架构，然后它自己还在不停的fork自己。显然有人是实践过了在多线程环境下的进程来进行fork。按照陈硕在书中的说法，glibc中的fork不是使用fork(2)系统调用，而是使用clone(2) syscall，按照clone在man手册中的解释，clone出来的子进程会共享父进程的某些资源，这与我曾经在linux上实践的感觉不一样。我很怀疑glibc中到底怎么搞得，是陈硕错了还是我错了。源码面前一览无遗，这是侯捷老师说的。

glibc的fork、vfork都调用了一个基础函数，叫做__fork，而clone调用则不一样。
fork的目录：nptl/sysdeps/unix/sysv/linux/pt-fork.c
vfork的目录：sysdeps/generic/vfork.c
clone的目录：sysdeps/unix/sysv/linux/arm/clone.S
clone是汇编哦

__fork函数的源文件在：
https://github.com/lattera/glibc/blob/master/nptl/sysdeps/unix/sysv/linux/fork.c

该文件中第133行是这样写的。
pid = INLINE_SYSCALL (fork, 0);
再看这里：https://www.gnu.org/software/hurd/glibc/fork.html
里面也写了，glibc的fork是使用了系统调用的，而且会rather bulky，会比单独的系统调用要重一些（这是肯定的撒）。
ok，这个问题解决了。fork肯定不是clone，可能是陈硕老师使用比较老版本的glibc吧，至少目前的版本不是这样的。

回到这一节的问题，在多线程中fork。一般来说是不能再多线程中去fork的，因为fork仅仅保留了当前调用fork的线程，其他的线程都消失了。陈硕老师还谈到，多线程下fork是危险的，因为可能出现其他线程对锁进行了加锁操作，而调用fork之后，该fork出来的独苗线程一旦尝试获得这个锁的时候，就会发生死锁！因此凡是带锁的函数我们都不能使用了，例如malloc、printf等。

一个小小的fork却是里面包含了不少的内容。我们继续回到网络通信库这个层面来。
我们知道nginx可以采用fork的方式，于是乎引起来的一个叫做"__惊群效应__"的东西。大致是在早先的内核中，如果我们在进程中监听了一个套接字，然后我们fork了多个服务子进程出来，那么当一个新的连接到来的时候，linux会唤醒所有的进程，所有的进程都会调用accept函数，但是有且仅有一个进程会accept成功，其他的会失败！也就是说很多系统资源被白白浪费了。
在现在的内核中，已经解决了这个问题，只会有一个进程会被唤醒。而且新的参数REUSEPORT也被开发了出来，伊塔监听套接字一旦使用了这个属性，就会默认可以由多个进程监听同一个端口，而内核会在多个程序中做自动负载，也就是说，我们可以采用完全没有父子关系的进程来平衡的处理同一个端口的事件。目前nginx也采用了REUSEPORT这个参数。以前nginx未使用，而淘宝开源Tengine使用，淘宝团队做出来的性能测试几乎使得nginx仅有Tengine的一半性能。
回到惊群效应，尽管在进程中已经被消灭了，但是放到epoll中呢？
是的，一个进程我们使用一个loop事件监听，然后注册到了epoll中去，然后我们再来fork他，当一个新的连接到来的时候，所有的epoll都会对这个东西响应，所以惊群效应还是存在的！

不过就我个人而言，这样的浪费是值得，至少多线程/多进程含有多epoll的原始意图就是充分利用多核性能，如果连接一般来说是__长连接或者生命周期很长的短链接__，我觉得这样的性能浪费是可以接受的，但是如果是异常短周期的短链接，那么这样的架构是不合适的。

