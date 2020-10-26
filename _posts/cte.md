title: CTE工作原理及其优化方法简介
date: 2017-11-01 11:18:02
tags: 数据库
categories: 数据库 SQL
---

> __摘要：__ SQL99标准中定义了common table expression，被简称CTE。作为一项重要的功能，多数主流数据库都对其进行了支持。本文简单分析Oracle的语法规则和运行过程，介绍GPDB优化器orca中CTE的优化方法。
  
## Oracle中CTE  
  
在ORACLE中，CTE也被称为With Clause或者Subquery Factoring。最早其出现于9.2版本，直到11.2版本实现了Recursive Subquery Factoring，才算是完整的支持了CTE。就实现层面来说，其Subquery Factoring与SQL99标准定义的CTE有所差别，自成标准。  
  
CTE拥有非常强的灵活性。在一个定义了CTE的查询中，后定义的表可以直接使用先定义的表；同样的，我们可以在主查询的任何一个地方使用CTE定义的表。这种方式使得CTE自带一种“程序化色彩”：如果我们把一条带有CTE的SQL看作一个“程序”，那么CTE就像这个”程序“的某个“函数”。而ORACLE显然也是非常希望它更好的发挥“函数”的角色：在12.1版本中，它直接允许在CTE中定义PL/SQL functions以及procedures。   
  
具体的，CTE可以被分为非递归（subquery factoring）和递归（recursive subquery factoring）。下图展示了Oracle的官方给出的句法图。  
  
query_block
![query_block.gif](/images/cte/query_block.gif)

subquery_factoring_clause
![subquery_factoring_clause.gif](/images/cte/subquery_factoring_clause.gif)

search_clause
![search_clause.gif](/images/cte/search_clause.gif)

cycle_clause
![cycle_clause.gif](/images/cte/cycle_clause.gif)
  
### subquery factoring  
贴上例子：  
```sql  
#语句0-1  
with ctable as (select * from t1) select c1 from ctable where c1 = 5;  
#语句0-2  
with ctable1 (a,b,c) as (select * from t1) select a from ctable1 where a = 5;  
#语句1  
select c1 from (select * from t1) table where c1 = 5;  
```  
语句0-1是一个最简单的cte写法，定义了一个叫做ctable的表，其实也就是t1表；语句0-2在语句0-1的基础上，指定了ctable的列名。从效果上来说，语句0-1与语句1是等效。同样的，除了递归cte以外，其它的cte都可以使用这种inline的方式改写成正常的语句，那么为什么还需要cte呢？  
  
如下所示一条SQL，我们将语句2-1中CTE定义的表展开到主查询中，得到了语句2-2。  
```SQL  
#语句2-1  
WITH   
  dept_costs AS (  
    SELECT dname, SUM(sal) dept_total  
    FROM   emp e, dept d  
    WHERE  e.deptno = d.deptno  
    GROUP BY dname),  
  avg_cost AS (  
    SELECT SUM(dept_total)/COUNT(*) avg  
    FROM   dept_costs)  
SELECT *  
FROM   dept_costs  
WHERE  dept_total > (SELECT avg FROM avg_cost)  
ORDER BY dname;  
  
#语句2－2  
SELECT *  
FROM   (  
    SELECT dname, SUM(sal) dept_total  
    FROM   emp e, dept d  
    WHERE  e.deptno = d.deptno  
    GROUP BY dname) dept_costs  
WHERE  dept_total > (SELECT avg FROM (  
    SELECT SUM(dept_total)/COUNT(*) avg  
    FROM   (  
            SELECT dname, SUM(sal) dept_total  
            FROM   emp e, dept d  
            WHERE  e.deptno = d.deptno  
            GROUP BY dname) dept_costs  
    ) avg_cost)  
ORDER BY dname;  
```  
对比语句2-2，语句2-1提取了公共子查询，SQL语句看起来非常清晰。如果CTE的表名字定义得当，你甚至可以不去读with定义的部分就可以知道这条SQL语句具体的功能是什么，这是CTE最直观的一个优势。  
具体的来说，CTE的引入可以带来的好处：  
 * 简化SQL语句的编写，不必多次重写相同的子查询；  
 * 子查询被抽取出来之后给优化器提供了新的优化选择；  
 * 最重要的是，使得SQL语句的主干部分逻辑清晰；  
  
在后面会提到，为什么说CTE为优化器带来了新的优化选择。  
  
### recursive subquery factoring 
除了常规的写法，oracle还支持递归的写法。例子如下：  
```SQL  
#语句3-1  
with rctable (rcid, rcname, rcleaderid) as  
    (  
    select id, name, leaderid from emp where emp.id = 1  
    union all  
    select emp.id , emp.name, emp.leaderid from emp, rctable where emp.leaderid = rctable.rcid  
    ) search depth first by rcid asc set num cycle rcid set iscyc to 'y' default 'n'  
select * from rctable;  
```  
上面的sql语句中，rctable是一张通过递归的CTE表。rctable表示在emp.id为1的行开始查询，获得其下属的行，并最终进行展示。执行结果如下所示。  
```SQL  
+------+--------+------------+------+-------+  
| rcid | rcname | rcleaderid | num  | iscyc |  
+------+--------+------------+------+-------+  
|    1 | A      |          0 |    1 | n     |  
|    2 | AA     |          1 |    2 | n     |  
|    5 | AAA    |          2 |    3 | n     |  
|    7 | AAA    |          5 |    4 | n     |  
|    8 | AAA    |          7 |    5 | n     |  
|    9 | AAAA   |          5 |    6 | n     |  
|   10 | AAAB   |          5 |    7 | n     |  
|   11 | AAAC   |          5 |    8 | n     |  
|   12 | AAAA   |          5 |    9 | n     |  
|    3 | AB     |          1 |   10 | n     |  
|    4 | ABA    |          3 |   11 | n     |  
|    6 | ABB    |          3 |   12 | n     |  
+------+--------+------------+------+-------+  
```  
  
Oracle的recursive subquery factoring不需要通过关键字来指定，是在解析过程中自动检测的，这一点与MYSQL（8.0及其以后，通过关键字RECURSIVE指定）是不同的。语句3－1我们定义了一张名为rctable的表，其包含了名为id，myname，leaderid的三个列。具体定义表的查询中，出现了关键字union all。  
union all的左支被称为anchor member，右支被称为recursive member。紧接着的search clause指定在执行过程中，中间结果按照rctable的id列以asc方式排序，进行深度优先搜索，并添加一个自增伪列，列名为num。cycle clause指定在执行过程中以rctable的id列的内容为标准，判断在搜索路径上是否形成环，并添加一个char类型的伪列，名为iscyc，默认值为’y‘。如果搜索路径上出现了相同id列值的行，则认为该行为环，该行不会进入到递归查询中而会直接输出，并且标记该伪列iscyc的值为‘n’。  
  
从逻辑上来说具体的执行过程是，首先执行anchor member的语句获得结果。以此次结果集为基准，每次从结果集中取出一条α作为recursive member中的定义表的唯一内容进行执行，获得的结果按一定的方式再次加入结果集，α进入到输出集。需要注意的是，在执行过程中，α如果与当前查询栈构成环，则不会执行,而直接加入输出集中。具体的执行过程按照深度或者广度优先的方式有细节上的不同。
  
从语法上看，recursive member中能出现的语句几乎囊括所有的查询语句，非常的灵活；但实际上Oracle为其定了非常严苛的使用限制。参考Oracle 11.2的[文档]([http://www.vldb.org/pvldb/vol8/p1704-elhelw.pdf])以及对其进行实际的测试，简单的总结罗列了对它的限制。具体的，其限制包括但不仅限于：
1. 递归的定义表，其只能出现在union all的右支中  
2. 定义表必须直接使用，并且不允许定义表出现在任何的子查询中  
3. 定义表出现在union all的右支中，则此语句必须且仅有左右支两个入口（例如，不能并列多个union）
4. 如果右支是一个join语句，则不能是full join；定义表只能出现在right join的右边；定义表只能出现在left join的左边
5. 定义表只能出现在union all的右支中，且不允许gourp by，distinct，以及任何聚合函数出现  
6. 如果右支中包含了递归表，但是却没有任何限制条件（意味着无限递归），则直接报错不进行执行（Oracle能够在语义上判断出无限执行的递归表）  
7. 如果我们定义了一张CTE的表，并期望它是一张递归执行的表，则其表定义必须含有列的定义  
8. search clause和cycle clause指定的列，必须是之前定义表时定义的列  
  
recursive subquery factoring的本质是深度或者广度优先的搜索，经过仔细的编写，你可以通过它实现很多有趣的功能。简单的，你可以实现阶乘，求最小公倍数；复杂的，你甚至可以用它来求数独的解，感兴趣的同学可以自行搜索一下。下面贴出了一个从oracle改写至OB的解数独的SQL语句以及其执行结果（使用mysql 8.0或者以上的同学，运行时请加上recursive关键字）。  
```SQL  
with x( s, ind, lev ) as  
( select sud, locate( ' ',sud ), 1  
  from ( select '53  7    6  195    98    6 8   6   34  8 3  17   2   6 6    28    419  5    8  79' sud from dual ) as no1  
  union all  
  select concat(substring( s, 1, ind - 1 ) , concat( z.zc , substring( s, ind + 1 ))), locate(' ', s, ind+1) , lev + 1  
  from x, z where ind > 0 and not exists ( select null from ( select zc lp   from z  ) as no2  
  where z.zc = substring( s, if((floor( ( ind - 1 ) / 9 ) * 9 + lp)=0, 1, (floor( ( ind - 1 ) / 9 ) * 9 + lp)), 1 )  
  or    z.zc = substring( s, if((mod( ind - 1, 9 ) - 8 + lp * 9)=0, 1, (mod( ind - 1, 9 ) - 8 + lp * 9)), 1 )  
  or    z.zc = substring( s, if((mod( floor( ( ind - 1 ) / 3), 3 ) * 3 + floor( ( ind - 1 ) / 27) * 27 + lp  + floor( ( lp - 1 ) / 3) * 6)=0, 1, (mod( floor( ( ind - 1 ) / 3), 3 ) * 3 + floor( ( ind - 1 ) / 27) * 27 + lp  + floor( ( lp - 1 ) / 3) * 6)) , 1 ) )  
)  
select s, ind, lev from x where ind = 0;  
+-----------------------------------------------------------------------------------+-----+-----+  
| s                                                                                 | ind | lev |  
+-----------------------------------------------------------------------------------+-----+-----+  
| 534678912672195348198342567859761423426853791713924856961537284287419635345286179 |   0 |  52 |  
+-----------------------------------------------------------------------------------+-----+-----+  
  
```  

## CTE的优化  
  
Greenplum是一个开源的MPP架构数据库系统，Orca是它的查询优化器。为了能够使CTE在更好的发挥作用，论文［1］以Orca为基础，实现了针对非递归的CTE进行了单独的优化。这里讲述了其对CTE的优化方法，以作启发之用。  
  
### CTE带来的问题  
  
要想实现对CTE优化，必须要解决三个问题。  
第一个是枚举所有备选计划的问题。  
```SQL  
#语句4-1  
WITH v as (SELECT i brand, i color FROM item WHERE i current price < 1000)  
    SELECT v1.*  
    FROM v v1, v v2, v v3  
WHERE v1.i brand = v2.i brand AND v2.i brand = v3.i brand AND v3.i color = ’red’;  
```  
图1  
![t1.png](/images/cte/t1.png) 
  
图1展示了语句4-1可能的几种计划。(a)是No inlining，(b)是all inline，(c)是Partial inlining。 当有多个CTE表被定义， 前面定义的CTE可以被后面定义的CTE直接使用，而主查询的任何一个地方都可以像正常的表一样使用各个CTE表，每个CTE的inline与否可能都会生成一个不同的计划。在这种复杂的情况下，怎样快速的罗列出所有正确的备选计划其实是一个难题。
  
第二个问题是死锁问题。  
假设我们的生成的计划后，执行顺序为CTE－>op1－>op2－>op3。假设在op2和op3的计算依赖于CTE的数据。假设op1包含某个逻辑，使得op1的输出一定是一个空，那么优化器在不知情的情况下可能将op1优化掉，计划执行变为了op2－>op3。由于op2依赖于CTE，而CTE永远不会执行，那么就会形成死锁问题。当然，这个问题的出现比较特化，与具体的执行方式紧密相关，不具有普遍性。
  
第三个问题是CTE的优化必须基于它出现位置的上下文来进行。 
我们再回过头来看图1。
第一种方式只进行一次CTE展开和执行，并将他的结果写盘，后续使用时，直接读取磁盘，使用该次执行的结果。这种方式使得CTE像一个真正的表一样（物化视图），甚至使得后续来到，定义了相同CTE的SQL也可以直接使用这个结果。但是这样使用方式是有问题的，对于v3来说，假设原表在i_color上拥有索引，那么我们非常遗憾的错失了一次利用索引的机会。
第二种方式则是在每一个使用CTE的地方，将CTE语句展开成一个子查询。使用这种方式，我们能够在v3上利用上索引，可是却又在v1，v2中多执行了一次一摸一样的重复计算。
第三种方式是一种理想的方式。其中，v1，v2只进行一次计算，共同利用一个CTE的执行结果，而v3上采用了inline的方式，利用索引的条件来优化执行。

假设我们可以枚举出所有不会形成死锁的计划，并且在生成计划的同时利用上下文条件对计划进行了优化改写，最后根据代码模型计算出来的各个计划的代价并选择出一个最优的计划，那么CTE的优化问题就得到了解决。
  
### CTE的优化方法  
  
图2  
![t2.png](/images/cte/t2.png) 
要想达到优化CTE的目的，首先需要解决的问题就是怎么在逻辑计划中表示CTE。如图2所示，论文提出了一种非常形象的表示方式，即以生产者、消费者的形式来的表示CTE。逻辑算子Producer表示生产者；Consumer来表示消费者；Anchor表示一个CTE定义表的作用域（例如Consumer[0]只能出现在以Anchor[0]为根的子树中）。后续还会出现一个叫做Sequence的算子，用于消除死锁。
之所以采用这样的方式来表示，是因为在这种表示基础上，经过简单规则即可以获得我们所有的备选计划。 
  
图3  
![t3.png](/images/cte/t3.png) 
  
假设我们原本的主计划如图3所示，我们经过如下的规则进行变化，则可以得到图4的结构。  
1. 将Anchor对应的Producer放入同一个group中，并且将Producer变为Sequence。  
2. Anchor同一个group中补入一个NoOp（空算子）  
3. 将Consumer对应Producer的代码放入到同一个group中    
  
图4  
![t4.png](/images/cte/t4.png) 

运用简单的算法对图4进行穷举即可获得所有的备选计划，但是其中有很多计划是无意义的。例如在group 0中选择了NoOp，在后续又选择了Consumer(0)算子。要确认一个计划是否为有效的计划，只要对计划进行一次遍历，保证一个生产者至少有一个消费者即可。最终，根据代价模型计算出的代价，选取最优的一个计划进行执行。
Sequence算子用来串联Producer和主计划。Sequence算子的特征是先执行左支（而且一定是一个Producer），再执行右支，而它左支Producer对应的Consumer一定只可能出现在以它为根节点的子树中，因而Producer一定在它的所有的Consumer执行之前进行了执行，也就不再存在死锁了。

需要注意的是，对于同一个Consumer来说，在谓词下压的时候，还可以通过OR将多个谓词关系组合并下压到Producer中。如图5所示。
图5  
![t5.png](/images/cte/t5.png) 
 
此外，在本身未定义CTE的一个计划中，抽取出公共的子查询。这种优化方式的难度在于如何在计划中找到公共的子查询或者公共的操作。当然我们也可以简化和特化这样的优化方法，例如，如图6所示优化，当发现计划中对同一个表的扫描，可以将其进行合并，避免重复操作。
图6  
![t6.png](/images/cte/t6.png) 
  
__*之前提到说CTE为优化器带来的新的选择，正是因为CTE避免了优化器自己去寻找公共子查询。对于优化器来说，CTE形式的SQL语句是减少多次重复执行相同子查询的最好输入。*__  
最后，对CTE的优化还需要考虑到包括数据分布，排序条件等，另外，论文中还针对CTE在大规模并行计算场景下的执行优化进行了阐述。想要了解更多具体的细节，可以参考论文[地址]([http://www.vldb.org/pvldb/vol8/p1704-elhelw.pdf])
