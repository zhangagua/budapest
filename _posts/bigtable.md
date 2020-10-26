---
title: 谷歌的马车们（二）：Bigtable
date: 2016-08-15 17:56:14
tags: 分布式系统
categories: 分布式系统
---

# 概述

Bigtable可以说是一个Nosql数据库，读取写入都是通过类似key－value对的形式来进行。
Bigtable的底层存储是GFS，然后在选主和容错处理上会依靠Chubby。Chubby的一个不保证准确的个人理解可以描述为：分布式环境下，一个全局可见且可靠的锁管理器，对外提供建锁、销锁、加锁、解锁等功能，提供功能的方式比较特殊，使用类似目录文件一样的树来做。然后Bigtable的逻辑数据单元成为Tablet，一个Tablet包含多个或者一个SSTable。
说是Bigtable（大表），其实里面是个Bigmap（大map）。


# 架构

![bigtable_architecture](/images/bigtable/bigtable_architecture.jpg)

如上所述，Bigtable侧靠Chubby，底层GFS。
Bigtable的模型是强一致性的。为啥？有人说因为一个SSTable的写盘操作，只写入到了一个tablet server上，既然是一个tablet server，那么肯定是强一致性，下面的文件系统到底怎么存、存多少份Bigtable并不关心。（其核心意思就是没有多副本{至少在bigtable层面来看是单份儿}，于是就是强一致性的了）

## 数据模型

![bigtable_data_model](/images/bigtable/bigtable_data_model.png)

A Bigtable is a sparse, distributed, persistent multidimensional sorted map.
论文中的原话，告诉我们一个非常重要的东西，那就是：它是个map。
千万千万切记不要按照传统的关系型数据库的存储模型来理解它，因为他是逻辑上看起来像表。比如之前就有谈论中出现了讨论NULL存在哪里的情况。
好的，也请不要叫他bigtable了，叫bigmap就行了，(⊙﹏⊙)b。

bigmap的映射关系：
>(row:string, column:string, time:int64)→string

按照这个映射关系，这里是行名+列名+时间戳就可以唯一确定一个sting出来。

### 列
接着论文提了一个叫做列族（column family）的概念。然后原文中是这么说的：
>Column keys are grouped into sets called column families, which form the basic unit of access control.

access control的最小单位是列族，即权限控制的最小单位。
>A column key is named using the following syntax:family:qualifier.

上面映射关系中column key的格式是：列族:限定词。很好理解，平时我们的列名就是一串字符串，这里的列名就是列族：限定词拼起来的一个字符串。
注意，列族建议最多只有几百个，但是列族里面的列可以建无所多个。

### 行
任意字符串。
行级别的锁，实现行级别事务。
不能跨表跨行的事务，导致后来谷歌又在Bigtable之上建造了其他的东西来完善事务，比如percolator。

### 时间戳
多版本控制必然需要时间戳。

# Tablet的位置

tablet的定位通过类似b+树的索引实现。root tablet的位置放置chubby中，由chubby保证这个位置的可靠性。root tablet指向其他meta tablet，meta tablet里面指向真实的tablet。用户第一次会依次从左到右找到具体的tablet。然后这些索引信息会缓存。
一个meta tablet大小为128MB，一个条目1KB；root tablet也是128M。那么一共可以有(2^27/2^10)^2=2^34个tablet。

![bigtable_data_model](/images/bigtable/bigtable_index_tablet.png)

## Tablet的紧缩

>Minor Compaction：把内存中的memtable冻结为SSTable的动作。
Merging Compaction：把部分SSTable合并为一个SSTable。
Major Compaction：把一个tablet上的所有数据合并为一个SSTable。
