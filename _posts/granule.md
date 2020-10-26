title: Oracle中granule的概念
date: 2018-04-08 11:18:02
tags: 数据库
categories: 数据库 SQL

---

# 关于granule
在oracle中，granule的官方解释为“A granule is the smallest unit of work when accessing data”，也就是oracle任务处理最小单位（task）将要的处理的数据集合大小。

## granule的类型
granule的类型有两种，第一种是block-based，第二种是partition-based。
oracle并行执行的机制是按照磁盘上的block ranges来进行并行执行，所以这一类叫做block-based granule。这种叫法是oracle独有的，block-based granule的概念与具体存储对象是否分区并无关系。要被读取的对象被划分成了多个granules，并且将它们交给PX server去进行执行；当一个granule被执行完成，另一个granule被交给PX server。其实这里的granule可以看作读取task，当一个task完成的时候，PX BLOCK ITERATOR会获得一个新的读取granule的task交给TSC算子去执行，直到所有的task完成。

partition-based granule，当仅有一个PX server负责一整个partition的数据读取。当一个operation要读取分区数>=DOP时，oracle的优化器会决定采用这种类型的granule。最常见的使用这种类型的granule的操作就是我们的partition wise join。此类型的granule在计划中显示为PX PARTITION RANGE ALL。

# 读懂oracle的并行的计划
```
运行语句
EXPLAIN PLAN FOR SELECT /*+PQ_DISTRIBUTE(r BROADCAST, BROADCAST) parallel(4)*/ r.c1 FROM r,s  WHERE r.c=s.c;
SELECT * FROM TABLE(DBMS_XPLAN.DISPLAY);
得到计划

------------------------------------------------------------------------------------------------------------------------------------
| Id  | Operation                  | Name     | Rows  | Bytes | Cost (%CPU)| Time     | Pstart| Pstop |    TQ  |IN-OUT| PQ Distrib |
------------------------------------------------------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT           |          |     1 |    39 |   607   (0)| 00:00:01 |       |       |        |      |            |
|   1 |  PX COORDINATOR            |          |       |       |            |          |       |       |        |      |            |
|   2 |   PX SEND QC (RANDOM)      | :TQ10002 |     1 |    39 |   607   (0)| 00:00:01 |       |       |  Q1,02 | P->S | QC (RAND)  |
|*  3 |    HASH JOIN BUFFERED      |          |     1 |    39 |   607   (0)| 00:00:01 |       |       |  Q1,02 | PCWP |            |
|   4 |     PX RECEIVE             |          |     1 |    26 |   303   (0)| 00:00:01 |       |       |  Q1,02 | PCWP |            |
|   5 |      PX SEND HYBRID HASH   | :TQ10000 |     1 |    26 |   303   (0)| 00:00:01 |       |       |  Q1,00 | P->P | HYBRID HASH|
|   6 |       STATISTICS COLLECTOR |          |       |       |            |          |       |       |  Q1,00 | PCWC |            |
|   7 |        PX BLOCK ITERATOR   |          |     1 |    26 |   303   (0)| 00:00:01 |     1 |     3 |  Q1,00 | PCWC |            |
|   8 |         TABLE ACCESS FULL  | R        |     1 |    26 |   303   (0)| 00:00:01 |     1 |     3 |  Q1,00 | PCWP |            |
|   9 |     PX RECEIVE             |          |     1 |    13 |   303   (0)| 00:00:01 |       |       |  Q1,02 | PCWP |            |
|  10 |      PX SEND HYBRID HASH   | :TQ10001 |     1 |    13 |   303   (0)| 00:00:01 |       |       |  Q1,01 | P->P | HYBRID HASH|
|  11 |       PX BLOCK ITERATOR    |          |     1 |    13 |   303   (0)| 00:00:01 |     1 |     3 |  Q1,01 | PCWC |            |
|  12 |        TABLE ACCESS FULL   | S        |     1 |    13 |   303   (0)| 00:00:01 |     1 |     3 |  Q1,01 | PCWP |            |
------------------------------------------------------------------------------------------------------------------------------------
```
以上面这个计划为范本，解释oracle的执行计划。首先，Id为1的operator被称作PX COORDINATOR，它其实是整个SQL的Query Coordinator。余下的2～11都是Parallel Servers将要执行的work。oracle的并行执行是按照生产者和消费者模型来进行的，调度保证同一时刻只激活一组（也就是一个生产者一个消费者）。当你指定DOP为10的时候，在某一个时刻，有10个PX server在执行某一个生产者，10个PX server在执行对应的消费者。
列TQ，全称为Table Queue。上面的计划TQ中名为Q1,00的一组PX server先执行了R表的扫描工作，将结果送到了Id为4的消费者；然后这组名为Q1,00的PX server变成了Q1,01 PX server（The Q1,00 set of parallel server processes then became the Q1,01 set of parallel server processes (which again are producers)）。Q1,01作为生产者继续生产数据，将数据发送到Id为9的消费者。
列IN-OUT，P->P指的是（from one parallel operation to another），P->S指的是（from a parallel operation to serial operation）。
列PQ Distrib，常见的方式包括hash，Broadcast，range，key，round robin。上面的计划展示的方式是Hybrid Hash。这种数据分发方式是oracle在12c引入的一种新的自适应的重分发方式。具体的来说，会根据直方图的采样数据，决定是采用broadcast＋round robin还是使用hash＋hash的方式。这个决定的决策者是STATISTICS COLLECTOR算子，一旦左边决定了自己方式，右边也要配合着选择对应的方式。这里引用oracle官方给出来的原文：“The query coordinator (QC) at runtime looks at the number of rows coming from table C, if the total number of rows is less than or equal to DOP*2 it decides to use broadcast distribution as the cost of broadcasting small number of rows will not be high. If the number of rows from table C is greater than DOP*2 the QC decides to use hash distribution for table C. The distribution method for table S is determined based on this decision. If table C is distributed by hash, so will table S. If table C is distributed by broadcast, table S will be distributed by round-robin.”[link](https://blogs.oracle.com/datawarehousing/adaptive-distribution-methods-in-oracle-database-12c#toc_1)
列Pstart和Pstop，在一级分区的情况下，代表的是具体的真实的物理分区号。在二级分区的情况下会变得复杂。例如在table scan full算子的后面出现的Pstart和Pstop值一般是一样的，这时候意味着出现了分区裁剪，它表明了这次table scan要扫描的分区数。


