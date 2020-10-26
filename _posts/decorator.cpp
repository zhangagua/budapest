# 几种简单的设计模式

tags： 温故知新

---
## 设计模式的基本原则

>开放-封闭原则：开放扩展，封闭修改
单一职责原则：就一个类而言，应该仅有一个引起它变化的原因
依赖倒转原则：高层模块和底层模块都应该依赖于抽象（接口），即细节依赖于抽象
里氏原则：子类必须要替换掉他们的父类
合成/聚合复用原则：尽量使用合成/聚合，尽量不要使用类继承
迪米特法则：如果两个类不必彼此直接通信，那么这两个类就不应当发生直接的相互作用。

上面总的说来有挺多条条的，不过他们的核心都是在为一件事情服务：解耦。一个程序是否有一个很好的设计，往往通过在它面临修改需求时候的表现在做判断。这种解耦的思想，不仅仅是我们计算机领域独有的，而是整个宏观人类世界的中抽象出来的一条规则。
往大了说，社会化大分工或者说经济全球化也都是遵循着这样一个原则——从各个政府到各个公司再到各个部门里面，各司其职各个机构才能正常运作。这样细化分工会极大的提高效率，方便管理，再者，当一个小部分出现问题时，部分的维修/整改/替换，可以降低对整体的影响。以前我们提到过两种内核的设计，Monolithic Kernel和Micro Kernel，这里又要提到它们，因为对于设计来说，这两大阵营的设计理念和哲学是值得我们思考和学习的，等往后有时间了得专门写篇文章来介绍这个由来已久的辩题。

## 单例模式

单例模式是一种非常有用的设计模式，在实际中用的也特别多。具体到实际中，有叫懒汉的实现方法和饿汉的实现方法。

```c++
//饿汉
class Singleton  
{  
    public:  
    static Singleton* GetInstance()  
    {  
        return &instance_;  
    }
    private:  
    Singleton()   //构造函数是私有的  
    {  
    }  
    static Singleton instance_;  
}; 
```
特点：你得知道类的静态变量是一个类所有的对象公有的
优点：线程安全，静态变量的初始化是在进入main之前就完成了的，肯定是线程安全撒
缺点：静态变量的初始化顺序不是确定的，依赖编译器初始化顺序才能正确跑的程序可不是什么好程序。换句话说，你既然敢这么用，必须要十分把握住这个类使用的时机，不然还没有初始化就被使用了，那肯定就挂了。
顺便说一句，什么说先初始化就是浪费空间的说话我不敢苟同。你都用单例了，跑完了程序最后这个对象一次都不用，那你为啥还要写这个类？

```c++
//懒汉，俗称lazy
class Singleton  
{  
    public:  
    static Singleton * GetInstance()  
    {  
        if(instance_ == NULL)  //判断是否第一次调用  
            instance_ = new CSingleton();  
        return instance_;  
    }
    ~Singleton()//记得在程序结束时，调用析构
    {
        if(instance_)
            delete instance_;
    }
    private:  
    Singleton()//构造函数是私有的  
    {  
    }  
    static Singleton *instance_;  
}; 
```
特点：第一次使用的时候建立对象
优点：浅显易懂还好使
缺点：线程不安全

```c++
//懒汉的另外写法,相对前面一个清爽一点，但是依然线程不安全。不安全的原因是编译器其实会对static局部变量展开，先要判断是否初始化，如果初始化了就不进入初始化流程，否则就开始初始化；多个线程可能同时进入到初始化流程，从而导致程序崩溃
class Singleton  
{  
    public:  
    static Singleton * GetInstance()  
    {  
        static Singleton instance;//依然是线程不安全
        return &instance;
    }
    ~Singleton()//记得在程序结束时，调用析构
    {
        if(instance_)
            delete instance_;
    }
    private:  
    Singleton()//构造函数是私有的  
    {
    }  
    static Singleton *instance_;  
}; 
```


## 简单工厂模式&&工厂方法&&抽象工厂模式

简单工厂

```c++
Class Person{ }
Class Student：public Person{ }
Class Teacher：public Person{ }
Class RenFactory { //造人工厂
    Person* Create(TYPE type){
        switch (type)
        {
	        case student_type:
	            return new Student();
	        case teacher_type:
	            return new Teacher();
	            ... ...
	    }
	}
}
```
简单工厂实际上是用的挺多的。在c++中，可以通过模板来替代传一个参数进去。如果你希望某一个产品完全只能由一个地方new出来，那么你可以把该类设置为私有，然后将工厂类申明为其友元，那么这个产品就只能是在工厂中产出。如果工厂再是一个单例模式，那么工厂类就可以记录一些有用的全局信息了。

工厂方法模式是指，将简单工厂中的唯一工厂抽象出一个工厂基类出来，你需要什么样的业务类，就先创建一个该类的工厂，用该工厂来产出你需要的业务类，而且该工厂仅仅只能产生这一种业务类。比起简单工厂，好处是每次添加新品的时候，不会__修改__原来的代码，只会__拓展__（增加一个新工厂类）。

抽象工厂方法模式是指，在工厂方法的基础上，每一个工厂可以产生多种业务类。

## 装饰者模式

```c++
#include<iostream>

class Base
{
    public:
    virtual void Do()=0;
};

class Person : public Base
{
    public:
        void set_component(Base* base){ base_ = base;};
        virtual void Do()
        { 
            std::cout<<"zbo"<<std::endl; 
            if(base_)
                base_->Do();
        };
    private:
        Base* base_;
};

class DecoratorA : public Base
{
    public:
        void set_component(Base* base){ base_ = base;};
        virtual void Do()
        { 
            std::cout<<"穿衣服"<<std::endl; 
            std::cout<<"装饰A特有动作"<<std::endl;
            if(base_)
                base_->Do();
        };
    private:
        Base* base_;
};

class DecoratorB : public Base
{
    public:
        void set_component(Base* base){ base_ = base;};
        virtual void Do()
        { 
            std::cout<<"穿裤子"<<std::endl; 
            std::cout<<"装饰B特有动作"<<std::endl;
            if(base_)
                base_->Do();
        };
    private:
        Base* base_;
};

int main()
{
    Person* zbo = new Person();
    DecoratorA* yifu = new DecoratorA();
    DecoratorB* kuzi = new DecoratorB();
    zbo->set_component(yifu);//(1)
    yifu->set_component(kuzi）;//(2)
    zbo->Do();//(3)
    return 1;
}
```
按照大话设计模式上的例子，实践出来，就是如上的代码，只不过奇怪的是，按照大话设计模式上，main里面的顺序会有一点变化，最终去调Do函数的是kuzi或者yifu，我觉得怪怪的，就给它改了。后来发现自己的理解错了，装饰类是用来包裹核心类的，所以确实是应该像大话设计模式那样，是装饰类来set（core_class）。
不过感觉大话设计模式中，这个模式讲的不是太好。它在里面给人的感觉是强调了多层次的装饰，但是实际上，我觉得应该要强调的是同一个类的多个装饰方法（类），这个才应该是装饰者模式的核心。看下面的代码。

```c++
Class PrintTicket
{
	virtual void Print(){}
}
Class CorePrintTicket
{
	void Print()
	{
		cout<<“商品名称，金额”<<endl;
	}
}
Class StarDecorator : public PrintTicket
{
    public:
	CorePrintTicket * StarDecorator(CorePrintTicket *p):
		 core_ptr(p){}
	void Print()
	{
		cout<<“****lovely star*****”<<endl;
		core_ptr->Print();
		cout<<“****lovely star*****”<<endl;
	}
	private:
	    CorePrintTicket * core_ptr_;
}
int main()
{
    PrintTicket * ptr1 = new StarDecorator ();
    Ptr1->Print();

    PrintTicket * ptr2 = new LineDecorator ();//实现方法类似
    Ptr2->Print();
}
```
灵活的切换/增加/删除一个装饰类，这才是这个模式的核心。

## 迭代器模式

c++里面迭代器是每个c++程序员每天都在用的，它居然是一种设计模式。(⊙﹏⊙)b。真是，孤陋寡闻啊哈哈。。
迭代器模式提供一种方法顺序访问一个聚合对象中的各种元素，而又不暴露该对象的内部表示。有了迭代器模式，我们可以方便的使用<algorithm>中的类似find函数。比如std::find (container.begin(), container。end(), 30)，完全不需要考虑这个容器是个什么容器，确实是方便。

## 建造者模式

又是一个非常有用的设计模式。将一个对象的建造过程和建造顺序分离，可以灵活的调整建造顺序来获得不同的对象。
```c++
class House
{
    vector<string> parts;
    public:    
    void Add(const string part)
    {
        parts.push_back(part);
    }
};

class Builder
{
    protected:
    House house_;
    public:
    virtual void Diji() = 0;    
    virtual void Kuangjia() = 0;    
    virtual void GaiDing() = 0;
    House GetHouse()
    {
        return  house_;
	}
};
class GrassBuilder:public Builder
{
    public:    
    virtual void Diji()
    {        
	    house_.Add("锄头挖的地基");
	}    
    virtual void Kuangjia()
    {        
	    house_.Add("泥巴做的结构");    
    }    
    virtual void GaiDing()
    {        
	   house_.Add(“草盖屋顶”);
	}
};
class Director{//包工头
    public:
    void Construct(Builder &builder)
    {        
       builder.Diji();
       builder.Kuangjia();
       builder.GaiDing();
    }
};
用法：
Director *director = new Director();
Builder *b1 = new GrassBuilder();
director->Construct(*b1);
House house = b1->GetHouse();}
```

## 外观模式（门面模式）

讲道理，这个模式应该放在建造者模式之前来讲。建造者模式有点像一个外观模式的抽象变种，但是将外观模式中的核心流程抽象出来了，核心流程抽象类下面有多种核心流程子类可以选择。
```c++
class Scanner
{  
	void Scan() { cout<<"词法分析"<<endl; }  
};  
class Parser
{  
	void Parse() { cout<<"语法分析"<<endl; }  
};  
class GenMachineCode
{  
	void GenCode() { cout<<"产生机器码"<<endl;}  
};  
//高层接口  Fecade
class Compiler  
{  
	void Run()
	{  
		Scanner scanner;  
		Parser parser;  
		GenMachineCode genMacCode;  
		scanner.Scan();  
		parser.Parse();  
		genMacCode.GenCode();  
	}
};  

int main()
{
    Compiler compiler;  
	compiler.Run();  
}
```

## 适配器模式

一个形象的例子，假设在英国买了一个iphone，回国后发现一个头疼的问题，那就是这个充电的插头貌似是英式的，和国内的插头口不一样，于是我就去网上买了一个转接头。这个转接头我们就是我们适配器了。具体写成代码来说，如下。

```c++
class  CnPlugs
{
    virtual void CnPin()
    {
        cout<<"插向中式插头"<<endl;
    }
}
class CnPlugsAdapter
{
    EnPlugs* en_plug_;
    public：
    void set_en_plug(EnPlugs* en_plug){en_plug_ = en_plug;}
    void CnPin()
    {
        if(en_plug_)
            en_plug_->EnPin();
        cout<<"插向中式插头"<<endl;
    }
}
class EnPlugs
{
    void EnPin()
    {
        cout<<"插向英式插头"<<endl;
    }
}
int main()
{
    EnPlugs *en_plugs = new EnPlugs();
    CnPlugs *cn_plus_adapter = new CnPlugsAdapter();
    cn_plus_adapter->set_en_plug(en_plugs);
    cn_plus_adapter->CnPin();
}
```

## 桥梁模式（桥接模式）

有这样的一个场景，我们有操作系统Linux，Max OS，windows，同时我们又有各类硬件平台，比如dell电脑，asus电脑等。
将两个角色之间的继承关系改为聚合关系，就是将它们之间的强关联改换成为弱关联。因此，桥梁模式中的所谓脱耦，就是指在一个软件系统的抽象化和实现化之间使用组合/聚合关系而不是继承关系，从而使两者可以相对独立地变化。这就是桥梁模式的用意。

```c++
class OS
{
	virtual void  OSImp()=0; 
}
class LinuxOS : public OS
{
	virtual void InitImp()
	{
		cout<<"Linux"<<endl;
	}
}
class Computer
{
	 virtual void InstallOS(OS *os) = 0;
}
class DellComputer : public Computer
{
    OS *os_;
	virtual void InstallOS( )
	{
	    os->InstallOS_Imp(); 
	}
}
```

## 策略者模式

把方法作为一个类抽出来一个基类，同样的一个算法我们可能有多中实现，具体用哪个，你只要传不同的子类进去就可以了。比如hash join和sorted join，都可以实现join的操作，但是他们是两个不同的算法。这种模式也是比较好理解，而且使用的比较多的O(∩_∩)O。

```c++
class Strategy
{
    public:
	virtual void Algrithm();
}
class Strategy1 : public Strategy{
    public:
	virtual void Algrithm()
	{
		cout<<"擦黑板"<<endl;
	}
}
class Context(){
    private:
    Strategy *st_ptr_;
    public:
	Context(Strategy *st_ptr) : st_ptr_(st_ptr){}
	void Action()
	{
		st_ptr_->Algrithm();
	}
}
用法：
ptr = new Context(new Strategy1());
ptr->Action();
```

## 命令模式

在撸码的时候偶，向某些对象发送请求，但是并不知道请求的接收者是谁，也不知道被请求的操作是哪个，我们只需在程序运行时指定具体的请求接收者即可。此时，可以使用命令模式来进行设计，使得请求发送者与请求接收者消除彼此之间的耦合,发送者与接收者之间没有直接引用关系。

说白了，所谓的命令模式起始就是在命令发出者和命令接受者之间加了一个中间层，这个中间层向上屏蔽命令执行者的具体动作，向下屏蔽命令发出者的一切信息。是不是有点像操作系统夹在硬件和应用软件之间？哈哈，一下就理解了吧，比画什么类图来的快多了。

```c++
// 烤肉师傅：命令执行者
class RoastCook
{
	void MakeMutton() { cout << "烤羊肉" <<endl; }
	void MakeChickenWing() { cout << “烤鸡翅膀”<<endl;}
};
//命令类
class Command
{
    public:
	Command(RoastCook* temp) { receiver = temp; }
	virtual void ExecuteCmd() = 0;
    protected:
	RoastCook* receiver;
};
//具体的动作类
class MakeMuttonCmd : public Command
{
    public:
	MakeMuttonCmd(RoastCook* temp) : Command(temp) {}
	virtual void ExecuteCmd() { receiver->MakeMutton(); }
};
//具体的动作类
class MakeChickenWing : public Command
{
    public:
	MakeMuttonCmd(RoastCook* temp) : Command(temp) {}
	virtual void ExecuteCmd() { receiver->MakeChickenWing(); }
};
//之前我们提到的中间类，用来转发命令
class Waiter
{	
    void SetCmd(Command* temp);
    void Notify()
    { 
	    for(aotu it : commandList)  it->ExecuteCmd() ;
    }
    void Cancle(Command* temp )
    {
	    m_commandList.remove(temp);
    }
   	……
}

int main()
{
    RoastCook cooker = new RoastCook();
    Command make_mutton_cmd = new MakeMuttonCmd(cooker);
    auto waiter = new Waiter;
    waiter->SetCmd(make_mutton_cmd);
    waiter->Notify();
}
```

## 观察者模式

这个模式和我之前写的一篇有点关系哦。linux下的io多路复用，多多少少都会用到观察者模式。

用一个现实生活中的例子来解释观察者模式。小张和小李都是公司的员工，他们都有事情需要向boss汇报，正巧的是boss外出，而且联系不上。于是小张和小李分别跑去找前台的接待小花，给小花说，如果boss回来了，一定要通知我，我有重要的事情向老板汇报。在这里，__小张和小李就是此模式中的观察者__。boss是否回来是被观察的行为，小花则是这个模式中的通知者。
一开始的时候我一直以为小花才是这个模式中的观察者，后来才发现自己错了。哈哈。
只要理解了指点，观察者模式还是很好理解的。

## 总结

设计模式在业务类代码中非常的常用，因为业务类代码常常涉及到需求修改、功能增减，所以选择一个好的设计模式可以更加从容的面对这类情况。设计模式的核心思想其实就是将类的功能尽量单一，类与类之间尽可能的解耦。一开始没有学习设计模式，代码写着写着再来看设计模式，会发现你平时代码设计中，多多少少都用到了某种设计模式，只是你不知道名字罢了。系统的学习一下设计模式，在以后的程序设计中能够明确的知道自己的场景，然后选择合适的设计模式，等修改或者要求拓展功能那一刻，你会发现它能够帮上你大忙。