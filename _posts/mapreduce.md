---
title: 谷歌的马车们（三）：MapReduce
date: 2016-08-27 20:09:14
tags: 分布式系统
categories: 分布式系统
---

前面两篇讲了谷歌的那些数据安放在了哪儿，但是想把这些瓶瓶罐罐真正的用起来，还是需要一个引擎的。mapreduce的角色便是这样的——把数据真正的用起来。

# 它是什么

>Mapreduce is a programming model and an associated implementation for processing and generating large data sets.

它是一种编程框架，实现了大规模的数据集的处理。其灵感来源于Lisp和其他的函数式编程语言中map和reduce的原意。（作为一名程序员可能真的要了解一下函数式编程语言了哦，比如渣打银行着重使用了Haskell）

# 一个例子

![mapreduce_overview](/images/mapreduce/mapreduce_overview.png)

上图中MapReduce展示一个map-reduce的流程。下文中讲的时间与图中的的序号一一对应。
>1. 用户程序首先调用的MapReduce库将输入文件分成M个数据片度，每个数据片段的大小一般从16MB到64MB(可以通过可选的参数来控制每个数据片段的大小)。然后用户程序在机群中创建大量的程序副本。(一台机器上的程序fork到另一台机器上？)
2. 这些程序中有一个称为master，其他的称为worker。master负责分配任务，woker负责具体计算。多个map和reduce任务被分配到worker上去。
3. 执行map任务的worker从磁盘读取文件，解析出key-value对作为输入，执行用户map函数，输出key-value pair到内存中。
4. 执行__分区函数__，将上一步的结果分为R个区，然后_写入磁盘_。把存储结果的位置汇报master，master负责将这些位置再发给reduce worker。（M个数据的结果每个都被划成R个区，M1的R1交给一个reduce，M2的R1也是交给它）
5. reduce worker收到master发的位置信息之后开始远程读取数据，然后对key进行__排序__；如果内存兜不住，那么使用外部排序。经过此步，相同key值的value聚合在了一起。
6. reduce worker程序遍历排序后的中间数据，对于每一个唯一的中间key值，它将这个key值和它相关的value的集合传递给用户自定义的reduce函数。执行后结果写盘。
7. 当所有的map和reduce任务都完成之后，master唤醒用户程序。

清理一下，具体流程是这样的：
map读数据->对数据执行用户map函数->对结果使用分区函数->map写盘->reduce读数据->reduce排序->排序后的聚合成< key, values>（注意是s哦）->对结果执行用户reduce函数->结果写盘

# 容错和优化
## 容错
worker的容错相对简单。由于master本身有对worker任务的感知，所以如果我们的worker出问题了，master可以肩负起恢复worker上原有任务的责任。
如果是master挂了就比较麻烦了。谷歌说的是在master本身会做checkpoint，然后尝试恢复，如果retry失败了，那么就返回客户端说明任务失败，是否再继续重启由客户端决定。

## 优化
mapreduce任务快慢会受到一个叫“落伍者”（straggler）的角色的影响。其大概意思就是，大部分任务都已经完成的情况下，总有那么几个或者一两个任务没有执行完成导致整个任务没法完成。原文中说到一种情况，如果一个机器的硬盘出了问题，在读取的时候要经常的进行读取纠错操作，导致读取数据的速度从30M/s降低到1M/s。
采用的决解方法是，当任务接近完成的时候：the master schedules backup executions of the remaining in-progress tasks。无论是备用还是之前的primary完成，都认为整个任务完成了。

# 其他
为了可靠和容错，mapreduce模型的中间结果是需要写盘的，输出结果也需要写盘。在多次的mapreduce任务中，每对map-reduce任务都会写盘，这个开销其实是很大的。在普通的hive-sql上，sql语句被解析成了多层次的mapreduce任务（DAG），__多次写盘和多次排序__过程，都让整个执行变得慢起来。
为了解决这个问题，像hive的Tez引擎，尝试将多次的map-reduce过程处理为一次map多次reduce，减少不必要的写盘操作（Tez的中间结果可选择放在内存还是磁盘），加速任务的执行。
关于排序这个问题，我认为在有必要的情况下才排序，应为在数据量很大的情况下，排序的开销也是不容小觑的。









