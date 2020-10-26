title: Oracle层次查询计划梳理
date: 2019-02-14 11:37:44
tags: databases
---

这里梳理oracle层次查询的计划与执行。
为了方便描述，层次查询第一轮获得的结果我们成为root set。
## 相关hint
相关hint都是成对出现的，具体如下：

|hint名称|含义|Oracle引入版本|
| :------| ------: | :------: |
|CONNECT_BY_COMBINE_SW|connect by融合start with的逻辑|10.2.0.4|
|NO_CONNECT_BY_COMBINE_SW||10.2.0.4|
|CONNECT_BY_COST_BASED|Instructs the optimizer to transform CONNECT BY based on cost.10.2.0.2|
|NO_CONNECT_BY_COST_BASED||10.2.0.2|
|CONNECT_BY_FILTERING|connect by查询逻辑交给外置join，也就是说connect by不做过滤|10.2.0.2|
|NO_CONNECT_BY_FILTERING|Prevents the SQL executor to filter data when perform CONNECT BY operation.|10.2.0.2|
|CONNECT_BY_ELIM_DUPS|Instructs the SQL executor to eliminate duplicated data when perform CONNECT BY operation.|11.2.0.2|
|NO_CONNECT_BY_ELIM_DUPS||11.2.0.2|
|CONNECT_BY_CB_WHR_ONLY|未知|10.2.0.5|
|NO_CONNECT_BY_CB_WHR_ONLY|未知|10.2.0.5|

相关连接
http://www.hellodba.com/Download/OracleSQLHints.pdf

## start with
### Oracle“隐藏”了一个TSC
select * from t1 start with c1 = 1 connect by prior c1 =  c2;
(Oracle计划一)
```
------------------------------------------------------------------------------------------------
| Id  | Operation                               | Name | Rows  | Bytes | Cost (%CPU)| Time     |
------------------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT                        |      |     3 |   117 |     4  (25)| 00:00:01 |
|*  1 |  CONNECT BY NO FILTERING WITH START-WITH|      |       |       |            |          |
|   2 |   TABLE ACCESS FULL                     | T1   |     4 |   156 |     3   (0)| 00:00:01 |
------------------------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - access("T1"."C2"=PRIOR "T1"."C1")
       filter("C1"=1)
```

看上去似乎是三个算子，但是实际上通过追踪oracle的trace日志可以发现还有一些其他的细节。
按照在trace日志中出现的先后顺序，有如下重要日志：

CBY: Costing for CONNECT BY method
START WITH query block SEL\$4 (#0) card: 1.000000 cost: 4.001383
FILTERING query block SEL\$3 (#0) card: 2.000000 cost: 7.025556
NO FILTERING query block SEL\$2 (#0) card: 4.000000 cost: 4.001394
Use no filtering (cost)
Costing START WITH combine with NO FILTERING:  START WITH predicate cost: 0.000008
Combine with START WITH chosen
 Adding start with query block SEL\$4 (#0) card: 1.000000
 Adding filtering query block SEL\$3 (#0) card: 2.000000

SCAN_TABLE_QKNTYP(0)
       ["C1", "T1"."C2", "T1"."C3"]
Query block SEL$4 (#0) processed

CONNECT_BY_IMPROVED_QKNTYP(1)
       ["connect$_by$_pump$_002"."prior c1 "]
SCAN_TABLE_QKNTYP(2)
       ["C1", "C2", "T1"."C3"]
IDENTITY_QKNTYP(3) on top of CONNECT_BY_IMPROVED_QKNTYP(1)
       ["connect$_by$_pump$_002"."prior c1 "]
IDENTITY_QKNTYP(4) on top of SCAN_TABLE_QKNTYP(2)
       ["C2", "C1", "T1"."C3"]
JOIN_QKNTYP(5) on top of IDENTITY_QKNTYP(3) //
       ["connect$_by$_pump$_002"."prior c1 ", "C2", "C1", "T1"."C3"]
Query block SEL\$3 (#0) processed


SCAN_TABLE_QKNTYP(6)
       ["C1", "T1"."C2", "T1"."C3"]
Query block SEL\$2 (#0) processed
Query block SET\$1 (#0) processed

IDENTITY_QKNTYP(7) on top of SCAN_TABLE_QKNTYP(6)
       ["T1"."C1", "T1"."C2", "T1"."C3"]
CONNECT_BY_IMPROVED_QKNTYP(8) on top of IDENTITY_QKNTYP(7) //
       ["T1"."C1", "T1"."C2", "T1"."C3", PRIOR "T1"."C1", LEVEL]



日志写的很清楚了，在代价计算的时候已经决定了使用no filtering的计划；但是在生成CBY的计划时，Oracle将filtering和no filtering都生成了。有一点可以佐证这个推断，通过加hint让它展示选择filtering的计划：

select /*+CONNECT_BY_FILTERING*/ * from t1 start with c1 = 1 connect by prior c1 =    c2;
(Oracle计划二)

```
----------------------------------------------------------------------------------
| Id  | Operation                 | Name | Rows  | Bytes | Cost (%CPU)| Time     |
----------------------------------------------------------------------------------
|   0 | SELECT STATEMENT          |      |     3 |   117 |    11  (19)| 00:00:01 |
|*  1 |  CONNECT BY WITH FILTERING|      |       |       |            |          |
|*  2 |   TABLE ACCESS FULL       | T1   |     1 |    39 |     3   (0)| 00:00:01 |
|*  3 |   HASH JOIN               |      |     2 |   104 |     6   (0)| 00:00:01 |
|   4 |    CONNECT BY PUMP        |      |       |       |            |          |
|   5 |    TABLE ACCESS FULL      | T1   |     4 |   156 |     3   (0)| 00:00:01 |
----------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - access("T1"."C2"=PRIOR "T1"."C1")
   2 - filter("C1"=1)
   3 - access("connect$_by$_pump$_002"."prior c1 "="C2")
```

(Oracle计划三)
```
explain plan for select /*+NO_CONNECT_BY_COMBINE_SW*/ * from t1 start with c1 = 1 connect by prior c1 =  c2;
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

-------------------------------------------------------------------------------------
| Id  | Operation                    | Name | Rows  | Bytes | Cost (%CPU)| Time     |
-------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT             |      |     3 |   117 |     8  (25)| 00:00:01 |
|*  1 |  CONNECT BY WITHOUT FILTERING|      |       |       |            |          |
|*  2 |   TABLE ACCESS FULL          | T1   |     1 |     9 |     3   (0)| 00:00:01 |
|   3 |   TABLE ACCESS FULL          | T1   |     4 |    36 |     3   (0)| 00:00:01 |
-------------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - access("T1"."C2"=PRIOR "T1"."C1")
   2 - filter("C1"=1)
```

计划一使用的是SEL$2 ＋ SEL$4 ＋ CONNECT_BY_IMPROVED_QKNTYP(8)
计划二使用的是SEL$3 ＋ SEL$4 ＋ CONNECT_BY_IMPROVED_QKNTYP(8)
但是从trace看，对于计划一，Oracle是“隐藏”了一个table scan的，计划展示的时候，filter("C1"=1)这个条件是挂在connect by算子上面的，也就是说它将start with的执行逻辑包含在自身。执行时，第一次执行table扫描，将会应用start with的过滤条件，后面扫描的结果将用于深度优先的查询。
计划一在Oracle的中出现的比计划二更晚一些。猜测可能是尽管优化器选择了no fitlering的计划执行，但是可能有的情况下最终只能以filtering的计划执行，所以两个计划都生成了，最终根据代码生成后检查判断来决定能否使用no filtering的计划。

### 如何让“隐藏”的TSC现形
#### 加入rownum
select t1.c1,level,rownum from t1 start with rownum = 1 connect by  prior c1 = c2;
(Oracle计划四)
```
--------------------------------------------------------------------------------------
| Id  | Operation                     | Name | Rows  | Bytes | Cost (%CPU)| Time     |
--------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT              |      |     4 |   156 |     8  (25)| 00:00:01 |
|   1 |  COUNT                        |      |       |       |            |          |
|*  2 |   CONNECT BY WITHOUT FILTERING|      |       |       |            |          |
|*  3 |    COUNT STOPKEY              |      |       |       |            |          |
|   4 |     TABLE ACCESS FULL         | T1   |     5 |   130 |     3   (0)| 00:00:01 |
|   5 |    COUNT                      |      |       |       |            |          |
|   6 |     TABLE ACCESS FULL         | T1   |     5 |   130 |     3   (0)| 00:00:01 |
--------------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   2 - access("C2"=PRIOR "C1")
   3 - filter(ROWNUM=1)
   

	C1	LEVEL	  ROWNUM
---------- ---------- ----------
	 3	    1	       1
	 4	    2	       2
```
注意，这里的connect by仍然是without filtering的计划，Oracle它终于将自己隐藏的table scan展示了出来。connect by算子从CONNECT BY NO FILTERING WITH START-WITH变成了CONNECT BY WITHOUT FILTERING。从计划中我们清晰的看到Rownum作为过滤条件放在了count算子中，而它的下面则是在之前那个曾经被隐藏的Table scan。后续在想要看清最真实的计划，可以通过在start with中添加rownum限制的计划来看。但是比较奇怪的是5号count算子，从语义上说，这个算子并没有起作用。

#### 加入hint
(Oracle计划五)
```
explain plan for select /*+NO_CONNECT_BY_COMBINE_SW*/ * from t1 start with c1 = 1 connect by prior c1 =  c2;
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

-------------------------------------------------------------------------------------
| Id  | Operation                    | Name | Rows  | Bytes | Cost (%CPU)| Time     |
-------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT             |      |     3 |   117 |     8  (25)| 00:00:01 |
|*  1 |  CONNECT BY WITHOUT FILTERING|      |       |       |            |          |
|*  2 |   TABLE ACCESS FULL          | T1   |     1 |     9 |     3   (0)| 00:00:01 |
|   3 |   TABLE ACCESS FULL          | T1   |     4 |    36 |     3   (0)| 00:00:01 |
-------------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - access("T1"."C2"=PRIOR "T1"."C1")
   2 - filter("C1"=1)
```
(Oracle计划六)
```
explain plan for select /*+CONNECT_BY_FILTERING*/ t1.c1,level,rownum from t1 start with rownum = 1 connect by  prior c1 = c2;
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());
-----------------------------------------------------------------------------------
| Id  | Operation                  | Name | Rows  | Bytes | Cost (%CPU)| Time     |
-----------------------------------------------------------------------------------
|   0 | SELECT STATEMENT           |      |     3 |   117 |    11  (19)| 00:00:01 |
|   1 |  COUNT                     |      |       |       |            |          |
|*  2 |   CONNECT BY WITH FILTERING|      |       |       |            |          |
|*  3 |    COUNT STOPKEY           |      |       |       |            |          |
|   4 |     TABLE ACCESS FULL      | T1   |     4 |    24 |     3   (0)| 00:00:01 |
|*  5 |    HASH JOIN               |      |     2 |    38 |     6   (0)| 00:00:01 |
|   6 |     CONNECT BY PUMP        |      |       |       |            |          |
|   7 |     TABLE ACCESS FULL      | T1   |     4 |    24 |     3   (0)| 00:00:01 |
-----------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   2 - access("C2"=PRIOR "C1")
   3 - filter(ROWNUM=1)
   5 - access("connect$_by$_pump$_002"."prior c1 "="C2")
```
通过加入hint我们也能将比较符合人类逻辑的计划展示出来。第一个hint是NO_CONNECT_BY_COMBINE_SW，表示connect by算子不容和start with的逻辑，第二个hint是CONNECT_BY_FILTERING，表示采用的connect by自身不带查询逻辑，仅作为查询栈的缓存以及查询的流程控制。

### 含有多表条件的start with
```
explain plan for select /*+NO_CONNECT_BY_FILTERING*/ * from t1 a join t1 b on a.c1 = b.c1 start with a.c2 > 1 and b.c1 <10 connect by nocycle prior b.c1 > a.c2;
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

------------------------------------------------------------------------------------------------
| Id  | Operation                               | Name | Rows  | Bytes | Cost (%CPU)| Time     |
------------------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT                        |      |     4 |   312 |     7  (15)| 00:00:01 |
|*  1 |  CONNECT BY NO FILTERING WITH START-WITH|      |       |       |            |          |
|*  2 |   HASH JOIN                             |      |     4 |   208 |     6   (0)| 00:00:01 |
|   3 |    TABLE ACCESS FULL                    | T1   |     5 |   130 |     3   (0)| 00:00:01 |
|   4 |    TABLE ACCESS FULL                    | T1   |     5 |   130 |     3   (0)| 00:00:01 |
------------------------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - access(INTERNAL_FUNCTION("A"."C2")<INTERNAL_FUNCTION(PRIOR "B"."C1"))
       filter("A"."C2">1 AND "B"."C1"<10)
   2 - access("A"."C1"="B"."C1")
```


## connect by
### 非标准用法
connect by中不包含prior表达式的用法被称为层次查询的非标用法。
#### 用法一
当层次查询的connect by中不使用prior符号时，我们可以认为这是层次查询的非标准用法。
select level from dual connect by level < 10;
这条语句也是西安银行的对层次查询的使用方法。多数情况下还可以使用这种方式来可以来快速的生成数据序列。这种类型的用法可以走特殊的执行路径以提高执行效率。
select c1 from t1 connect by level < 3;
每次进行深度优先查询的时候结果都是整个t1表的数据。假设t1表有5行数据，那么root set就是5行。
我们拿这里每一行进行查询，每一行获得结果5行，它们level为2。
select c1 from t1 connect by level < 3;结果为5+5*5=30
select c1 from t1 connect by level < 4;结果为5+5*5+5*5*5=155
这种方法生成数据就更快了。
#### 用法二
select level from dual connect by level < 10;
select level from dual connect by rownum < 10;
结果相同，但是
select c1 from t1 connect by level < 5;
select c1 from t1 connect by rownum < 5;
结果大不相同。
```
--------------------------------------------------------------------------------------
| Id  | Operation                     | Name | Rows  | Bytes | Cost (%CPU)| Time     |
--------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT              |      |     5 |    65 |     3   (0)| 00:00:01 |
|   1 |  COUNT                        |      |       |       |            |          |
|*  2 |   CONNECT BY WITHOUT FILTERING|      |       |       |            |          |
|   3 |    TABLE ACCESS FULL          | T1   |     5 |    65 |     3   (0)| 00:00:01 |
--------------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   2 - filter(ROWNUM<5)
```
后面一节会分析出来一个结论：挂在CONNECT BY WITHOUT FILTERING的filter对于root set是不生效的。这里先直接拿来用，我们看一下Oracle中执行的结果：
```
	C1	LEVEL
---------- ----------
	 3	    1
	 3	    2
	 3	    3
	 3	    4
	 1	    1
	 4	    1
	 0	    1
		    1
```
root set的5行全部都在，然后在第二轮进行查询的时候，其结果只有一行（c1=3这一行）。很好理解，就是拿第一行去进行查询，由于层次查询是深度优先，每次查询的结果都是完整的t1的内容，然后将其中第一行再进行深度优先查询，其结果也是t1内容；接着又拿第一行去进行查询。这种查询方法内存非常容易蹦，因为整个深度优先路径上的所有节点都被保存了。

#### 非标用法的相关讨论
http://www.sqlsnippets.com/en/topic-11821.html
http://www.orafaq.com/forum/t/77347/
http://www.rampant-books.com/book_0601_sql_coding_styles.htm

### 特殊的过滤方法
select t1.c1, t1.c2 , level from t1 connect by  prior c1 = c2 and level = 0;
select t1.c1, t1.c2 , level from t1 connect by  prior c1 = c2 and c2 = 2 and prior c2 = 2;
从trace日志中可以发现，这个计划确实只有两个算子，一个是CONNECT BY WITHOUT FILTERING算子，一个是table scan算子。
我们可以看到connect by算子挂了一个access("C2"=PRIOR "C1")＋filter(level=0)，两条SQL的结果是一样的。
```
	C1	   C2	   LEVEL
---------- ---------- ----------
	 3	    2	       1
		    2	       1
	 0	    2	       1
	 1	    2	       1
	 4	    3	       1
```
计划中的filter并没有过滤掉这些行，至少对于这个算子来说，它的filter比较特殊，仅在root set结果获得之后进行查询时生效。
```
explain plan for select /*+NO_CONNECT_BY_FILTERING NO_CONNECT_BY_COMBINE_SW*/ t1.c1, t1.c2 , level from t1 connect by  prior c1 = c2 and c2 = 2 and prior c2 = 2 start with t1.c2 > 0;
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

-------------------------------------------------------------------------------------
| Id  | Operation                    | Name | Rows  | Bytes | Cost (%CPU)| Time     |
-------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT             |      |    25 |  1300 |     8  (25)| 00:00:01 |
|*  1 |  CONNECT BY WITHOUT FILTERING|      |       |       |            |          |
|*  2 |   TABLE ACCESS FULL          | T1   |     5 |   130 |     3   (0)| 00:00:01 |
|   3 |   TABLE ACCESS FULL          | T1   |     5 |   130 |     3   (0)| 00:00:01 |
-------------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - access("C2"=PRIOR "C1" AND PRIOR "C2"=2)
       filter("C2"=2)
   2 - filter("T1"."C2">0)
```
### 子查询
#### 非相关子查询
##### 不包含prior/不包含start with的情况
```
explain plan for select t1.c1, t1.c2, level from t1 connect by c1!=(select count(*) from t2);
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

-------------------------------------------------------------------------------------
| Id  | Operation                    | Name | Rows  | Bytes | Cost (%CPU)| Time     |
-------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT             |      |     5 |   130 |     3   (0)| 00:00:01 |
|*  1 |  CONNECT BY WITHOUT FILTERING|      |       |       |            |          |
|   2 |   TABLE ACCESS FULL          | T1   |     5 |   130 |     3   (0)| 00:00:01 |
|   3 |   SORT AGGREGATE             |      |     1 |       |            |          |
|   4 |    TABLE ACCESS FULL         | T2   |     1 |       |     3   (0)| 00:00:01 |
-------------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - filter("C1"<> (SELECT COUNT(*) FROM "T2" "T2"))
```

##### 包含prior表达式不包含start with的情况
```
explain plan for select t1.c1, t1.c2, level from t1 connect by prior c1!=(select count(*) from t2);
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

-------------------------------------------------------------------------------------
| Id  | Operation                    | Name | Rows  | Bytes | Cost (%CPU)| Time     |
-------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT             |      |     5 |   130 |     3   (0)| 00:00:01 |
|*  1 |  CONNECT BY WITHOUT FILTERING|      |       |       |            |          |
|   2 |   TABLE ACCESS FULL          | T1   |     5 |   130 |     3   (0)| 00:00:01 |
|   3 |   SORT AGGREGATE             |      |     1 |       |            |          |
|   4 |    TABLE ACCESS FULL         | T2   |     1 |       |     3   (0)| 00:00:01 |
-------------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - filter( (SELECT COUNT(*) FROM "T2" "T2")<>PRIOR "C1")
```
##### 包含start with的情况
```
explain plan for select t1.c1, t1.c2, level from t1 start with rownum < 10 connect by c1!=(select count(*) from t2);
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

-------------------------------------------------------------------------------------
| Id  | Operation                    | Name | Rows  | Bytes | Cost (%CPU)| Time     |
-------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT             |      |     6 |   234 |     8  (25)| 00:00:01 |
|*  1 |  CONNECT BY WITHOUT FILTERING|      |       |       |            |          |
|*  2 |   COUNT STOPKEY              |      |       |       |            |          |
|   3 |    TABLE ACCESS FULL         | T1   |     5 |   130 |     3   (0)| 00:00:01 |
|   4 |   COUNT                      |      |       |       |            |          |
|   5 |    TABLE ACCESS FULL         | T1   |     5 |   130 |     3   (0)| 00:00:01 |
|   6 |   SORT AGGREGATE             |      |     1 |       |            |          |
|   7 |    TABLE ACCESS FULL         | T2   |     1 |       |     3   (0)| 00:00:01 |
-------------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - filter("C1"<> (SELECT COUNT(*) FROM "T2" "T2"))
   2 - filter(ROWNUM<10)
```
在上面这个语句中，加入CONNECT_BY_FILTERING hint，它仍然不会生效，计划维持不变。从上面三个计划中可知，connect by这个算子可以有三个输入。可以推测一下执行流程：
1. 最左边为start with的一支，这一支只会执行一次，获得root set；
2. 由于没有查询条件，root set的第一行进行深度优先查询的时候，结果集为t1整个表；尽管没有查询条件，但是有一个filter条件；查询结果第一行不满足自身c1!=子查询这个条件直接过滤掉，满足条件直接作为下一次查询的节点。
3. 拿到查询节点，再执行步骤2。

##### 结论
CONNECT_BY_FILTERING算子是一个三支输入的算子，第一支只执行一次为start with，第二支和第三支会被反复执行。
显然，上面这个结论是错的，来自Oracle的光速打脸。
```
explain plan for select t1.c1, t1.c2 , level from t1 start with rownum < 2 connect by c1=(select count(*) from     t2) and c2!=(select count(*) from     t2);
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

-------------------------------------------------------------------------------------
| Id  | Operation                    | Name | Rows  | Bytes | Cost (%CPU)| Time     |
-------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT             |      |     2 |   104 |     8  (25)| 00:00:01 |
|*  1 |  CONNECT BY WITHOUT FILTERING|      |       |       |            |          |
|*  2 |   COUNT STOPKEY              |      |       |       |            |          |
|   3 |    TABLE ACCESS FULL         | T1   |     5 |   130 |     3   (0)| 00:00:01 |
|   4 |   COUNT                      |      |       |       |            |          |
|   5 |    TABLE ACCESS FULL         | T1   |     5 |   130 |     3   (0)| 00:00:01 |
|   6 |   SORT AGGREGATE             |      |     1 |       |            |          |
|   7 |    TABLE ACCESS FULL         | T2   |     1 |       |     3   (0)| 00:00:01 |
|   8 |   SORT AGGREGATE             |      |     1 |       |            |          |
|   9 |    TABLE ACCESS FULL         | T2   |     1 |       |     3   (0)| 00:00:01 |
-------------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - filter("C1"= (SELECT COUNT(*) FROM "T2" "T2") AND "C2"<> (SELECT 
              COUNT(*) FROM "T2" "T2"))
   2 - filter(ROWNUM<2)
```
所以真正的结论是，
CONNECT_BY_FILTERING算子是一个多支输入的算子，第一支只执行一次为start with，第二支负责深度优先要查找的原始计划，后续的很多支作为filter支会被反复执行。

#### 相关子查询
为什么带子查询连一个join都不给。
```
select /*+GATHER_PLAN_STATISTICS*/ t1.c1, t1.c2 , level from t1 start with rownum < 2 connect by c1=(select max(b.c2) from t2 B where t1.c1 = B.c1);
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.display_cursor(FORMAT=>'ALLSTATS LAST ALL +outline'));

----------------------------------------------------------------------------------------------------------------------------------------------------------
| Id  | Operation                    | Name | Starts | E-Rows |E-Bytes| Cost (%CPU)| E-Time   | A-Rows |   A-Time   | Buffers |  OMem |  1Mem | Used-Mem |
----------------------------------------------------------------------------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT             |      |      1 |        |       |     8 (100)|          |      1 |00:00:00.01 |      42 |       |       |          |
|   1 |  CONNECT BY WITHOUT FILTERING|      |      1 |        |       |            |          |      1 |00:00:00.01 |      42 |  2048 |  2048 | 2048  (0)|
|*  2 |   COUNT STOPKEY              |      |      1 |        |       |            |          |      1 |00:00:00.01 |       7 |       |       |          |
|   3 |    TABLE ACCESS FULL         | T1   |      1 |      5 |   130 |     3   (0)| 00:00:01 |      1 |00:00:00.01 |       7 |       |       |          |
|   4 |   COUNT                      |      |      1 |        |       |            |          |      5 |00:00:00.01 |       7 |       |       |          |
|   5 |    TABLE ACCESS FULL         | T1   |      1 |      5 |   130 |     3   (0)| 00:00:01 |      5 |00:00:00.01 |       7 |       |       |          |
|   6 |   SORT AGGREGATE             |      |      4 |      1 |    26 |            |          |      4 |00:00:00.01 |      28 |       |       |          |
|*  7 |    TABLE ACCESS FULL         | T2   |      4 |      1 |    26 |     3   (0)| 00:00:01 |      1 |00:00:00.01 |      28 |       |       |          |
----------------------------------------------------------------------------------------------------------------------------------------------------------
 
Query Block Name / Object Alias (identified by operation id):
-------------------------------------------------------------
 
   1 - SEL$1
   2 - SEL$4
   3 - SEL$4 / T1@SEL$4
   4 - SEL$2
   5 - SEL$2 / T1@SEL$2
   6 - SEL$6
   7 - SEL$6 / B@SEL$6
 
Outline Data
-------------
 
  /*+
      BEGIN_OUTLINE_DATA
      IGNORE_OPTIM_EMBEDDED_HINTS
      OPTIMIZER_FEATURES_ENABLE('12.2.0.1')
      DB_VERSION('12.2.0.1')
      ALL_ROWS
      OUTLINE_LEAF(@"SEL$6")
      OUTLINE_LEAF(@"SEL$2")
      OUTLINE_LEAF(@"SEL$5")
      OUTLINE_LEAF(@"SEL$3")
      OUTLINE_LEAF(@"SEL$4")
      OUTLINE_LEAF(@"SET$1")
      OUTLINE_LEAF(@"SEL$1")
      NO_ACCESS(@"SEL$1" "connect$_by$_work$_set$_009"@"SEL$1")
      NO_CONNECT_BY_FILTERING(@"SEL$1")
      FULL(@"SEL$4" "T1"@"SEL$4")
      FULL(@"SEL$3" "connect$_by$_pump$_003"@"SEL$3")
      FULL(@"SEL$3" "T1"@"SEL$3")
      LEADING(@"SEL$3" "connect$_by$_pump$_003"@"SEL$3" "T1"@"SEL$3")
      USE_MERGE_CARTESIAN(@"SEL$3" "T1"@"SEL$3")
      PQ_FILTER(@"SEL$3" SERIAL)
      FULL(@"SEL$2" "T1"@"SEL$2")
      FULL(@"SEL$5" "B"@"SEL$5")
      FULL(@"SEL$6" "B"@"SEL$6")
      END_OUTLINE_DATA
  */
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   2 - filter(ROWNUM<2)
   7 - filter("B"."C1"=:B1)
 
Column Projection Information (identified by operation id):
-----------------------------------------------------------
 
   1 - "C1"[NUMBER,22], "T1"."C2"[NUMBER,22], "T1"."C1"[NUMBER,22], LEVEL[4]
   2 - "C1"[NUMBER,22], "T1"."C2"[NUMBER,22], ROWNUM[8]
   3 - "C1"[NUMBER,22], "T1"."C2"[NUMBER,22]
   4 - "C1"[NUMBER,22], "T1"."C2"[NUMBER,22], ROWNUM[8]
   5 - "C1"[NUMBER,22], "T1"."C2"[NUMBER,22]
   6 - (#keys=0) MAX("B"."C2")[22]
   7 - (rowset=256) "B"."C1"[NUMBER,22], "B"."C2"[NUMBER,22]
```

## where条件的处理
### 单表where条件的处理
#### 不含有伪列的情况
```
explain plan for select /*+CONNECT_BY_FILTERING*/ t1.c1, t1.c2 , level from t1 where c1 > 1 start with c2>1 connect by prior c1 = c1;
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

-----------------------------------------------------------------------------------
| Id  | Operation                  | Name | Rows  | Bytes | Cost (%CPU)| Time     |
-----------------------------------------------------------------------------------
|   0 | SELECT STATEMENT           |      |    10 |   390 |    11  (19)| 00:00:01 |
|*  1 |  FILTER                    |      |       |       |            |          |
|*  2 |   CONNECT BY WITH FILTERING|      |       |       |            |          |
|*  3 |    TABLE ACCESS FULL       | T1   |     5 |   130 |     3   (0)| 00:00:01 |
|*  4 |    HASH JOIN               |      |     5 |   195 |     6   (0)| 00:00:01 |
|   5 |     CONNECT BY PUMP        |      |       |       |            |          |
|   6 |     TABLE ACCESS FULL      | T1   |     5 |   130 |     3   (0)| 00:00:01 |
-----------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - filter("C1">1)
   2 - access("C1"=PRIOR "C1")
   3 - filter("C2">1)
   4 - access("connect$_by$_pump$_002"."prior c1 "="C1")
```
#### 含有level
```
explain plan for select /*+CONNECT_BY_FILTERING*/ t1.c1, t1.c2 , level from t1 where level > 1 start with c2>1 connect by prior c1 = c1;
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

-----------------------------------------------------------------------------------
| Id  | Operation                  | Name | Rows  | Bytes | Cost (%CPU)| Time     |
-----------------------------------------------------------------------------------
|   0 | SELECT STATEMENT           |      |    10 |   390 |    11  (19)| 00:00:01 |
|*  1 |  FILTER                    |      |       |       |            |          |
|*  2 |   CONNECT BY WITH FILTERING|      |       |       |            |          |
|*  3 |    TABLE ACCESS FULL       | T1   |     5 |   130 |     3   (0)| 00:00:01 |
|*  4 |    HASH JOIN               |      |     5 |   195 |     6   (0)| 00:00:01 |
|   5 |     CONNECT BY PUMP        |      |       |       |            |          |
|   6 |     TABLE ACCESS FULL      | T1   |     5 |   130 |     3   (0)| 00:00:01 |
-----------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - filter(LEVEL>1)
   2 - access("C1"=PRIOR "C1")
   3 - filter("C2">1)
   4 - access("connect$_by$_pump$_002"."prior c1 "="C1")
```
connect_by_isleaf，connect_by_root，connect_by_iscycle，level的表现都一致。
sys_connect_by_path函数在此禁用。

#### 含有rownum
```
explain plan for select /*+CONNECT_BY_FILTERING*/ t1.c1, t1.c2 , level from t1 where rownum < 10 start with c2>1 connect by prior c1 = c1;
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

------------------------------------------------------------------------------------
| Id  | Operation                   | Name | Rows  | Bytes | Cost (%CPU)| Time     |
------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT            |      |    10 |   390 |    11  (19)| 00:00:01 |
|   1 |  COUNT                      |      |       |       |            |          |
|*  2 |   FILTER                    |      |       |       |            |          |
|*  3 |    CONNECT BY WITH FILTERING|      |       |       |            |          |
|*  4 |     TABLE ACCESS FULL       | T1   |     5 |   130 |     3   (0)| 00:00:01 |
|   5 |     COUNT                   |      |       |       |            |          |
|*  6 |      HASH JOIN              |      |     5 |   195 |     6   (0)| 00:00:01 |
|   7 |       CONNECT BY PUMP       |      |       |       |            |          |
|   8 |       TABLE ACCESS FULL     | T1   |     5 |   130 |     3   (0)| 00:00:01 |
------------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   2 - filter(ROWNUM<10)
   3 - access("C1"=PRIOR "C1")
   4 - filter("C2">1)
   6 - access("connect$_by$_pump$_002"."prior c1 "="C1")
```
### 两表where条件的处理
#### 不含伪列，含join条件
```
explain plan for select /*+CONNECT_BY_FILTERING*/ * from t1 a, t1 b where a.c1=b.c1 start with a.c2>1 connect by nocycle prior b.c1 > a.c2;
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

----------------------------------------------------------------------------------
| Id  | Operation                 | Name | Rows  | Bytes | Cost (%CPU)| Time     |
----------------------------------------------------------------------------------
|   0 | SELECT STATEMENT          |      |     5 |   520 |    22  (19)| 00:00:01 |
|*  1 |  CONNECT BY WITH FILTERING|      |       |       |            |          |
|*  2 |   HASH JOIN               |      |     4 |   312 |     6   (0)| 00:00:01 |
|*  3 |    TABLE ACCESS FULL      | T1   |     5 |   195 |     3   (0)| 00:00:01 |
|   4 |    TABLE ACCESS FULL      | T1   |     5 |   195 |     3   (0)| 00:00:01 |
|*  5 |   HASH JOIN               |      |     1 |    91 |    14  (15)| 00:00:01 |
|   6 |    MERGE JOIN             |      |     1 |    52 |    11  (19)| 00:00:01 |
|   7 |     SORT JOIN             |      |     4 |    52 |     7  (15)| 00:00:01 |
|   8 |      CONNECT BY PUMP      |      |       |       |            |          |
|*  9 |     SORT JOIN             |      |     5 |   195 |     4  (25)| 00:00:01 |
|  10 |      TABLE ACCESS FULL    | T1   |     5 |   195 |     3   (0)| 00:00:01 |
|  11 |    TABLE ACCESS FULL      | T1   |     5 |   195 |     3   (0)| 00:00:01 |
----------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - access(INTERNAL_FUNCTION("A"."C2")<INTERNAL_FUNCTION(PRIOR 
              "B"."C1"))
   2 - access("A"."C1"="B"."C1")
   3 - filter("A"."C2">1)
   5 - access("A"."C1"="B"."C1")
   9 - access(INTERNAL_FUNCTION("connect$_by$_pump$_003"."prior b.c1 
              ")>INTERNAL_FUNCTION("A"."C2"))
       filter(INTERNAL_FUNCTION("connect$_by$_pump$_003"."prior b.c1 
              ")>INTERNAL_FUNCTION("A"."C2"))


explain plan for select /*+CONNECT_BY_FILTERING*/ * from t1 a, t1 b where a.c1=b.c1 and a.c1 > 10 start with a.c2>1 connect by nocycle prior b.c1 > a.c2;
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

-----------------------------------------------------------------------------------
| Id  | Operation                  | Name | Rows  | Bytes | Cost (%CPU)| Time     |
-----------------------------------------------------------------------------------
|   0 | SELECT STATEMENT           |      |     5 |   585 |    22  (19)| 00:00:01 |
|*  1 |  FILTER                    |      |       |       |            |          |
|*  2 |   CONNECT BY WITH FILTERING|      |       |       |            |          |
|*  3 |    HASH JOIN               |      |     4 |   312 |     6   (0)| 00:00:01 |
|*  4 |     TABLE ACCESS FULL      | T1   |     5 |   195 |     3   (0)| 00:00:01 |
|   5 |     TABLE ACCESS FULL      | T1   |     5 |   195 |     3   (0)| 00:00:01 |
|*  6 |    HASH JOIN               |      |     1 |    91 |    14  (15)| 00:00:01 |
|   7 |     MERGE JOIN             |      |     1 |    52 |    11  (19)| 00:00:01 |
|   8 |      SORT JOIN             |      |     4 |    52 |     7  (15)| 00:00:01 |
|   9 |       CONNECT BY PUMP      |      |       |       |            |          |
|* 10 |      SORT JOIN             |      |     5 |   195 |     4  (25)| 00:00:01 |
|  11 |       TABLE ACCESS FULL    | T1   |     5 |   195 |     3   (0)| 00:00:01 |
|  12 |     TABLE ACCESS FULL      | T1   |     5 |   195 |     3   (0)| 00:00:01 |
-----------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - filter("A"."C1">10)
   2 - access(INTERNAL_FUNCTION("A"."C2")<INTERNAL_FUNCTION(PRIOR 
              "B"."C1"))
   3 - access("A"."C1"="B"."C1")
   4 - filter("A"."C2">1)
   6 - access("A"."C1"="B"."C1")
  10 - access(INTERNAL_FUNCTION("connect$_by$_pump$_003"."prior b.c1 
              ")>INTERNAL_FUNCTION("A"."C2"))
       filter(INTERNAL_FUNCTION("connect$_by$_pump$_003"."prior b.c1 
              ")>INTERNAL_FUNCTION("A"."C2"))
```
where中的join条件被天然的下推到了左右两支，参看2号算子5号算子的条件；同时，非join condition的条件，被拆解了，作为connect by算子输出时候的filter。

#### 含伪列，不含／含join条件
与单表查询中where出现伪列表现一致，作为filter挂在connect by算子头上。
与上一下节结论一样，就是join条件单独拆出来下推到左右两边，其他filter条件挂在connect by算子头上的filter。

#### where中出现子查询
```
explain plan for select /*+CONNECT_BY_FILTERING*/ * from t1 a, t1 b where a.c1=(select count(*) from t2) start with a.c2>1 connect by nocycle prior b.c1 > a.c2;
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

-----------------------------------------------------------------------------------
| Id  | Operation                  | Name | Rows  | Bytes | Cost (%CPU)| Time     |
-----------------------------------------------------------------------------------
|   0 | SELECT STATEMENT           |      |    56 |  6552 |    40  (10)| 00:00:01 |
|*  1 |  FILTER                    |      |       |       |            |          |
|*  2 |   CONNECT BY WITH FILTERING|      |       |       |            |          |
|   3 |    MERGE JOIN CARTESIAN    |      |    25 |  1950 |    12   (0)| 00:00:01 |
|*  4 |     TABLE ACCESS FULL      | T1   |     5 |   195 |     3   (0)| 00:00:01 |
|   5 |     BUFFER SORT            |      |     5 |   195 |     9   (0)| 00:00:01 |
|   6 |      TABLE ACCESS FULL     | T1   |     5 |   195 |     2   (0)| 00:00:01 |
|   7 |    MERGE JOIN              |      |    31 |  2821 |    26   (8)| 00:00:01 |
|   8 |     SORT JOIN              |      |    25 |  1950 |    13   (8)| 00:00:01 |
|   9 |      MERGE JOIN CARTESIAN  |      |    25 |  1950 |    12   (0)| 00:00:01 |
|  10 |       TABLE ACCESS FULL    | T1   |     5 |   195 |     3   (0)| 00:00:01 |
|  11 |       BUFFER SORT          |      |     5 |   195 |     9   (0)| 00:00:01 |
|  12 |        TABLE ACCESS FULL   | T1   |     5 |   195 |     2   (0)| 00:00:01 |
|* 13 |     SORT JOIN              |      |    25 |   325 |    13   (8)| 00:00:01 |
|  14 |      CONNECT BY PUMP       |      |       |       |            |          |
|  15 |   SORT AGGREGATE           |      |     1 |       |            |          |
|  16 |    TABLE ACCESS FULL       | T2   |     1 |       |     3   (0)| 00:00:01 |
-----------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   1 - filter("A"."C1"= (SELECT COUNT(*) FROM "T2" "T2"))
   2 - access(INTERNAL_FUNCTION("A"."C2")<INTERNAL_FUNCTION(PRIOR 
              "B"."C1"))
   4 - filter("A"."C2">1)
  13 - access("connect$_by$_pump$_005"."prior b.c1 ">"A"."C2")
       filter("connect$_by$_pump$_005"."prior b.c1 ">"A"."C2")
```
类似于之前的非join条件的表达式，无论是相关还是非相关子查询，最终都是挂在connect by算子头上的filter。

#### 多表where条件的处理
同两表

#### 带Order by / Group by
```
explain plan for select /*+CONNECT_BY_FILTERING*/ * from t1 a join t1 b on a.c1 = b.c1 where rownum < 10 start with a.c2 > 1 and b.c1 <10 connect by nocycle prior b.c1 > a.c2 order by level;
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

-------------------------------------------------------------------------------------
| Id  | Operation                    | Name | Rows  | Bytes | Cost (%CPU)| Time     |
-------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT             |      |     4 |   312 |    21  (15)| 00:00:01 |
|   1 |  SORT ORDER BY               |      |     4 |   312 |    21  (15)| 00:00:01 |
|   2 |   COUNT                      |      |       |       |            |          |
|*  3 |    FILTER                    |      |       |       |            |          |
|*  4 |     CONNECT BY WITH FILTERING|      |       |       |            |          |
|*  5 |      HASH JOIN               |      |     3 |   156 |     6   (0)| 00:00:01 |
|*  6 |       TABLE ACCESS FULL      | T1   |     4 |   104 |     3   (0)| 00:00:01 |
|*  7 |       TABLE ACCESS FULL      | T1   |     4 |   104 |     3   (0)| 00:00:01 |
|   8 |      COUNT                   |      |       |       |            |          |
|*  9 |       HASH JOIN              |      |     1 |    65 |    14  (15)| 00:00:01 |
|  10 |        MERGE JOIN            |      |     1 |    39 |    11  (19)| 00:00:01 |
|  11 |         SORT JOIN            |      |     3 |    39 |     7  (15)| 00:00:01 |
|  12 |          CONNECT BY PUMP     |      |       |       |            |          |
|* 13 |         SORT JOIN            |      |     5 |   130 |     4  (25)| 00:00:01 |
|  14 |          TABLE ACCESS FULL   | T1   |     5 |   130 |     3   (0)| 00:00:01 |
|  15 |        TABLE ACCESS FULL     | T1   |     5 |   130 |     3   (0)| 00:00:01 |
-------------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   3 - filter(ROWNUM<10)
   4 - access(INTERNAL_FUNCTION("A"."C2")<INTERNAL_FUNCTION(PRIOR "B"."C1"))
   5 - access("A"."C1"="B"."C1")
   6 - filter("A"."C2">1 AND "A"."C1"<10)
   7 - filter("B"."C1"<10)
   9 - access("A"."C1"="B"."C1")
  13 - access(INTERNAL_FUNCTION("connect$_by$_pump$_004"."prior b.c1 
              ")>INTERNAL_FUNCTION("A"."C2"))
       filter(INTERNAL_FUNCTION("connect$_by$_pump$_004"."prior b.c1 
              ")>INTERNAL_FUNCTION("A"."C2"))
              

explain plan for select /*+CONNECT_BY_FILTERING*/ a.c1 from t1 a join t1 b on a.c1 = b.c1 where rownum < 10 start with a.c2 > 1 and b.c1 <10 connect by nocycle prior b.c1 > a.c2 group by a.c1;
SELECT PLAN_TABLE_OUTPUT FROM TABLE(DBMS_XPLAN.DISPLAY());

-------------------------------------------------------------------------------------
| Id  | Operation                    | Name | Rows  | Bytes | Cost (%CPU)| Time     |
-------------------------------------------------------------------------------------
|   0 | SELECT STATEMENT             |      |     4 |   156 |    21  (15)| 00:00:01 |
|   1 |  HASH GROUP BY               |      |     4 |   156 |    21  (15)| 00:00:01 |
|   2 |   COUNT                      |      |       |       |            |          |
|*  3 |    FILTER                    |      |       |       |            |          |
|*  4 |     CONNECT BY WITH FILTERING|      |       |       |            |          |
|*  5 |      HASH JOIN               |      |     3 |   117 |     6   (0)| 00:00:01 |
|*  6 |       TABLE ACCESS FULL      | T1   |     4 |   104 |     3   (0)| 00:00:01 |
|*  7 |       TABLE ACCESS FULL      | T1   |     4 |    52 |     3   (0)| 00:00:01 |
|   8 |      COUNT                   |      |       |       |            |          |
|*  9 |       HASH JOIN              |      |     1 |    52 |    14  (15)| 00:00:01 |
|  10 |        MERGE JOIN            |      |     1 |    39 |    11  (19)| 00:00:01 |
|  11 |         SORT JOIN            |      |     3 |    39 |     7  (15)| 00:00:01 |
|  12 |          CONNECT BY PUMP     |      |       |       |            |          |
|* 13 |         SORT JOIN            |      |     5 |   130 |     4  (25)| 00:00:01 |
|  14 |          TABLE ACCESS FULL   | T1   |     5 |   130 |     3   (0)| 00:00:01 |
|  15 |        TABLE ACCESS FULL     | T1   |     5 |    65 |     3   (0)| 00:00:01 |
-------------------------------------------------------------------------------------
 
Predicate Information (identified by operation id):
---------------------------------------------------
 
   3 - filter(ROWNUM<10)
   4 - access(INTERNAL_FUNCTION("A"."C2")<INTERNAL_FUNCTION(PRIOR "B"."C1"))
   5 - access("A"."C1"="B"."C1")
   6 - filter("A"."C2">1 AND "A"."C1"<10)
   7 - filter("B"."C1"<10)
   9 - access("A"."C1"="B"."C1")
  13 - access(INTERNAL_FUNCTION("connect$_by$_pump$_004"."prior b.c1 
              ")>INTERNAL_FUNCTION("A"."C2"))
       filter(INTERNAL_FUNCTION("connect$_by$_pump$_004"."prior b.c1 
              ")>INTERNAL_FUNCTION("A"."C2"))
```
order by/group by都是针对connect by的结果进行的。

## 结论
大方向上，oracle的connect by现在有no filtering和filtering两种方式；OB目前的实现接近于filtering模式。connect by算子的第一支为start with，第二支为反复查询的一支，后面的多支为connect by clause中的子查询服务。
connect by中多表的情况下:
a. 仅仅对where中的join condition特殊处理，需要下压左右两端；其他where条件（包括伪列，rownum，子查询等），作为connect by算子的filter挂在其上面；order by／group by等，也是针对层次查询的结果来进行的。
b. start with的所有条件挂在第一支
c. connect by clause的所有条件挂在自己，包括子查询
