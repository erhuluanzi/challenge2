	## challenge2 -- concurrent matrix multiplication
> This is our team's second challenge!

```
Challenge! The prime sieve is only one neat use of message passing between a 
large number of concurrent programs. Read C. A. R. Hoare, "Communicating 
Sequential Processes," Communications of the ACM 21(8) (August 1978), 666-
667, and implement the matrix multiplication example.
```

### Part 1: Survey - read the paper
我们可以在google scholar上下载到这篇论文pdf版，正版不侵权嗯。本论文的核心就是如何充分利用计算机的并发能力，进行更高效的并行计算；然后论文给出了许多可以使用并行程序解决的经典问题。

传统的存储程序式计算执行的程序都是单个顺序执行的指令序列。随着计算机硬件的发展，多核技术开始出现，如果能够有效利用多核中多个处理单元的计算能力，那么就能够达到比传统单处理器更快速、更可靠、功能更强大、存储容量更大、更为经济的结果。应该说，本文的出发点是硬件的发展推动了软件方面新的算法与程序架构的变革。

如何才能够有效利用多核处理器的机器来完成单个任务呢？如果是不同的任务很好理解，分别放在不同的处理器上运行即可。但是如果同一个程序，这就需要在软件编写方面与其他配套机制的支持互相配合了，肯定会遇到一个任务如何切分，不同部分如何合并等问题。最核心一点就是处理器之间应该有通信与同步机制：在当时普遍使用的通信机制有共享存储方式，然而这种方式比较简单粗暴，很可能引起程序不正确而且硬件设计方面也很困难；同步机制当时主要有信号量、条件临界区、管程、队列等等，但是具体选择哪个来使用或说哪个更好也没有统一的标准。于是这篇paper就想用一种简单的解决方法处理以上所有问题。

#####本文的基本假设大致如下，总结了一下没有很完全：#####
1. 使用Dijkstra's guarded commands，个人理解就是一个形式系统用于抽象描述程序。我没有特别细致地去了解这个形式语言到底是怎么回事，但是能看懂伪代码应该就可以了。
2. 使用parallel command，也是基于Dijkstra的parbegin语法，目测也是用于描述程序的。
3. 进程间的通信，一个进程指定第二个进程作为输出目标，同时第二个进程指定第一个进程作为输入源。类似于网络中建立连接的传输。而且这样的通信机制没有缓冲区，必须等传输双方都准备好才开始通信，如果没准备好就一直等待。
4. 用文中规定写好的程序既可以运行在传统的共享内存的单机上，也可以运行在用网络连接的集群上。没有递归，不同进程之间也不会有共享全局变量。

在文中的3～5节讨论了如何用上述机制来解决实际问题，而且作者也说了：`The reader is invited to skip the examples which do not interest him.` 所以后面的内容我们实际上只用关心我们需要关心的"concurrent matrix multiplication"问题，其余的我会一笔带过然后也没有一个个详细地去读。

第2节介绍了一种形式语言系统，后面的程序都用它表示，然而过于繁杂我只看了一下大概标记都是什么意思就没再看了。3～5节涉及了数据传输处理、数据结构表示与递归、管程和进程调度（有熟悉的有限缓冲区问题、哲学家就餐问题）。第6节的6.1就是lab 4中的primes测试程序的出处，基本思想就是利用多个进程并行来模拟顺序队列的操作。6.2就是我们本次challenge关注的核心内容 -- 并行矩阵乘法。

##### An Iterative Array: Matrix Multiplication #####
问题：给定维数为3的方形矩阵A，输入为行向量IN，一共3个输入流，每个流代表IN的一列(实际上就一个数)。输出也为3个流，分别为IN x A的三个列(其实每列也是一个数)

解决算法：
1. 分成东、南、西、北、中，这五组节点。（颇有中国特色）
2. 北部节点负责提供累加的初始值0。
3. 西部节点是输入流，对应矩阵的三行，标记为x, y, z。（我的理解是源源不断地提供(x0, y0, z0), (x1, y1, z1)这样的三元数对，就能达到不断处理向量和A矩阵相乘的情况）
4. 东部节点实际上是一行的终止，只负责接收西边传来的值，没有其他作用。
5. 南部节点为一列累加运算之后得到的值，就是输出流。
6. 中部节点接收它西边和北边节点传来的值，西边的值乘上该节点的值加上北边传来的值之后，把西边的值原样传给东边，把累加值传给南边。

利用更加规范的表述可能会好一点...
```
想象整个系统为5X5的M矩阵（除去四角共21个节点，利用"a:b"表示[a, b]区间）
WEST: M(1:3, 0)
NORTH: M(0, 1:3)
EAST: M(1:3, 4)
SOUTH: M(4, 1:3)
CENTER: M(1:3, 1:3)
然后WEST与SOUTH是用户提供的输入输出流。
NORTH: 所有节点都为0
EAST: M(i, 3)接受西边(M(i, 2))传来的值，什么都不做。
CENTER: 节点M(i, j)接受西边(M(i, j-1))传来的值x，直接传x给东边(M(i, j+1))。并接收北边M(i-1, j)传来的值sum，计算A(i, j)*x + sum，传给南边(M(i+1, j))
```
然后，就没有然后了...

##### JOS实现上述算法的可行性 #####

* JOS的IPC机制满足论文中提到的："进程间的通信，一个进程指定第二个进程作为输出目标，同时第二个进程指定第一个进程作为输入源。"
* JOS的IPC机制是阻塞式发送和接收，保证了每个进程一次只会处理和另一个进程间的传输。满足了"这样的通信机制没有缓冲区，必须等传输双方都准备好才开始通信，如果没准备好就一直等待。"的条件。
* JOS的IPC机制是在两个进程之间的，和所在CPU没有关系。所以既可以在单处理器的机器上跑，也可以在多处理器的机器上跑。然而JOS并不能支持集群的情况，因为IPC的共享物理页面机制建立在所有处理器共享内存的基础之上。 

### Part 2: 代码实现
前提：C＝B＊A

#####框架设计
考虑论文中的算法，我将整个程序分成master、center_proc、north_proc、west_proc、south_proc、east_proc、collector等7种进程，其中master用于协调算法的开始与结束，collector用于从south_proc收集结果，重新组织成矩阵，其余5种进程定义同Hoare论文提到的。

最初我没有考虑让整个算法连续运行，设计的算法是master每次发送一个B的行向量给west_proc，等这个流被处理完，从south_proc接收得到的行向量，然后发送下一个流。于是设计了一个wait_for_data函数，用法如下：

```c
	/*
	 * 等待所需要的全部数据
	 * 只能实现单个流的乘法，已废弃
	 */
	struct RequiredData {
	    envid_t from;
	    int value;
	    int is_set;
	};
	
	int
	wait_for_data (int rdc, struct RequiredData* rdv) {
	    int i, value;
	    int count = 0;
	    envid_t who;
	    while (count != rdc) {
	        value = ipc_recv(&who, 0, 0);
	        for (i = 0; i < rdc; i ++)
	            if (rdv[i].from == who && !rdv[i].is_set)
	                break;
	        if (i == rdc) {
	            panic("wait_for_data: unexpected recv: %x got %x from %x\n", sys_getenvid(), value, who);
	            continue;
	        }
	        rdv[i].value = value;
	        rdv[i].is_set = 1;
	        count++;
	    }
	    return 0;
	}
	
	int
	example(void) {
	    cprintf("i am master environment %08x\n", thisenv->env_id);
	    envid_t id = fork();
	    if (id == 0) {
	        cprintf("i am child  environment %08x\n", thisenv->env_id);
	        struct RequiredData rd[2] = {
	            {0x00001001, 0, 0},
	            {0x00001001, 0, 0}
	        };
	        wait_for_data(2, rd);
	        cprintf("rd[0] = %d\n", rd[0].value);
	        cprintf("rd[1] = %d\n", rd[1].value);
	        return 0;
	    }
	    ipc_send(id, 1, 0, 0);
	    ipc_send(id, 2, 0, 0);
	    return 0;
	}
```

但是这种算法会导致整个阵列中只有NORDER个节点处于活跃状态，其余节点一直在等待输入，效率低下，所以放弃了这个思路，转而使用队列来记录一个节点得到的所有数据，然后每当获得了足够一轮数据处理所需的数据时，进行处理以及发送，可以保证阵列中尽可能多的节点处于活跃状态。每个proc的框架如下：

```c
proc:

	// 数据定义部分
    const int NDATA = 1;    // 需求数据数目
    int i, j;               // 循环变量
    int x, y;               // 当前proc的env_id在env_id_mat中的位置
    int value, count = 0;   // 数据和获得数据计数
    envid_t who;            // 信息来源
    envid_t from[NDATA];    // 需求数据来源
    int data[NDATA];        // 一轮中收集的数据
    struct Queue queue[NDATA];  // 数据队列
    int ready = 1;          // 是否收集了一轮所需的数据

    // 进程初始化
    // 从master获得PROCSTART信号，并设置x, y
    value = ipc_recv(&who, 0, 0);
    if (who == env_id_mat[0][0] && (value & PROCMASK) == PROCSTART) {
        x = (value & 0xFF00) >> 8;
        y = (value & 0XFF);
        cprintf("east_proc(%d, %d) %08x: started successfully.\n", x, y, thisenv->env_id);
    }
    else
        panic("east_proc: unexpected recv: %x got %x from %x\n", sys_getenvid(), value, who);

    // 进程设置
    // 设置需求数据来源
    /*
     * 在这里对from进行设置，
     * from[i] = env_id 表示从env_id获取数据，存放在i位置上，
     * 其对应的data和queue的下标为i。
     */

    // 进程主体
    while (count < NORDER) {
        // 检查数据的身份
        value = ipc_recv(&who, 0, 0);
        for (i = 0; i < NDATA; i++)
            if (from[i] == who)
                break;
        if (i == NDATA)
            panic("proc: unexpected recv: %08x got %x from %08x\n", sys_getenvid(), value, who);
        q_push(&queue[i], value);

        // 检查是否收集了用于一轮处理的数据
        ready = 1;
        for (i = 0; i < NDATA; i++)
            if (q_empty(&queue[i]))
                ready = 0;
        if (ready) {
            // 数据提取
            for (i = 0; i < NDATA; i++) {
                data[i] = q_top(&queue[i]);
                q_pop(&queue[i]);
            }
            // 数据处理
            /*
             * 此处会将一轮所需数据保存在data中，
             * 进行处理后发送给对应的env_id。
             */
            count++;
        }
    }
    cprintf("proc(%d, %d) %08x: return successfully.\n", x, y, thisenv->env_id);
    return 0;

以上便实现了除master之外的proc。
```
#####master设计

对于master，我们需要实现的是创建proc阵列，记录proc阵列的每个位置的env_id，协调算法的开始与结束，获取输入输出。由于Hoare的算法中需要让每个proc知道自己东南西北的env_id，所以我采用一个矩阵env_id_mat进行记录。

这里我定义了两个个信息，

	#define PROCMASK 0xFFFF0000
	#define PROCSTART 0xAB010000
	#define PROCEND 0xABFF0000

高16位表示这个信息的种类，低16位携带其他信息。但是写到最后发现用处不是很大，也就没有扩展这个机制，只是在一开始用于开始进程时和最终标示进程结束使用了。
由于这个进程在被创建的时候不知道自己在阵列中的位置，我就把这个信息加载在start信号中由master一起传送给proc。
考虑到proc一旦启动就会开始接收和发送信息，所以在master启动它们的时候要保持一个拓扑序，以防止proc与master发送的信息发生冲突。这里我采取的是south->east->center->north->west。

考虑fork机制，它会复制全局变量，但是不会继承后续的修改，所以我们对env_id_mat进行的修改无法被之前创建的proc得知。原本我想设计一个创建proc的顺序回避这个问题，但是在创建center_proc时发现，它既需要知道自己发送对象（east、south），也需要知道自己的接收对象（north、west），所以在拓扑图上存在环。由于fork产生的env_id是累加的，所以我选择在master开始时就先填充env_id_mat，然后在实际创建proc时对env_id进行检查，以此回避了共享内存的问题，同时也保证了安全性。

#####collector设计
之前都是由master收集结果（在向west发送了B中所有的流之后，master开始从south_proc接收数据，并组织成C矩阵，得到结果），但是在测试时发现，有时候会卡死在一个地方，程序陷入死锁状态。然而有时候运行10次又有一次能够结束并得到正确结果。经过排查，发现在程序中存在这样的环：master->west_proc->center_proc->south_proc->master，最后一个master直有当全部send工作结束才会开始recv，这就导致了当一个数据流的每一个节点都充满数据，并等待ipc_send时，依赖关系存在一个环，于是就死锁了。所以最后我们又增加了一个collector收集N＊N的结果并输出。由于collector是无条件进行recv，并于最后向master发送PROCEND信号，所以解决了死锁问题。collector的整体框架还是proc，所以也比较容易添加。

在调试中遇到的麻烦主要是ipc机制使处于ipc_recv状态的env为不可执行，而JOS在无可执行env时会掉入monitor（其实不是缺陷，因为这时确实会导致JOS挂起），有时会不知道程序是在哪里丢失了信息，所以在整个程序中添加了大量调试输出，直接查看log就可以了解整个程序的运行流程了。

### Part 3: TEST
完全遵循Hoare论文中的例子，A矩阵采用3＊3方形矩阵，B其实是相应3个输入流，理论上可以无限长，只是我们测试用了3＊3方形矩阵。原因是JOS现在文件系统储存与加载文本文件不太容易，并且JOS又没有malloc机制支持动态分配内存，所以我们只好事先给定了A、B矩阵。改变维数只需要修改NORDER参数即可，但是还是要实现给定两个相乘的矩阵。
```c
int A[NORDER][NORDER] = {
    {1, 5, 6},
    {2, 5, 7},
    {2, 1, 4}
};
int B[NORDER][NORDER] = {
    {1, 2, 7},
    {4, 5, 1},
    {3, 8, 9}
};
```
输出结果为：
```
19 22 48
16 46 63
37 64 110
```
经验算结果正确，为B＊A的值。所以我们的实现应该是正确的。

下面进行进一步的验证，使用10＊10的两个方阵A、B进行矩阵相乘。NORDER=10
```c
int A[NORDER][NORDER] = {
    {1, 5, 6, 1, 1, 1, 5, 6, 1, 1},
    {2, 5, 7, 1, 1, 1, 5, 6, 1, 1},
    {2, 1, 4, 1, 1, 1, 5, 6, 1, 1},
    {1, 1, 1, 1, 1, 1, 5, 6, 1, 1},
    {1, 1, 1, 1, 1, 1, 5, 6, 1, 1},
    {1, 5, 6, 1, 1, 1, 5, 6, 1, 1},
    {2, 5, 7, 1, 1, 1, 5, 6, 1, 1},
    {2, 1, 4, 1, 1, 1, 5, 6, 1, 1},
    {1, 1, 1, 1, 1, 1, 5, 6, 1, 1},
    {1, 1, 1, 1, 1, 1, 5, 6, 1, 1}
};
int B[NORDER][NORDER] = {
    {1, 2, 7, 1, 1, 1, 2, 7, 1, 1},
    {4, 5, 1, 1, 1, 1, 2, 7, 1, 1},
    {3, 8, 9, 1, 1, 1, 2, 7, 1, 1},
    {1, 1, 1, 1, 1, 1, 2, 7, 1, 1},
    {1, 1, 1, 1, 1, 1, 2, 7, 1, 1},
    {1, 2, 7, 1, 1, 1, 2, 7, 1, 1},
    {4, 5, 1, 1, 1, 1, 2, 7, 1, 1},
    {3, 8, 9, 1, 1, 1, 2, 7, 1, 1},
    {1, 1, 1, 1, 1, 1, 2, 7, 1, 1},
    {1, 1, 1, 1, 1, 1, 2, 7, 1, 1}
};
```
的到结果为：
```
42 48 100 24 24 24 120 144 24 24
39 72 115 24 24 24 120 144 24 24
60 90 162 34 34 34 170 204 34 34
28 37 69 17 17 17 85 102 17 17
28 37 69 17 17 17 85 102 17 17
42 48 100 24 24 24 120 144 24 24
39 72 115 24 24 24 120 144 24 24
60 90 162 34 34 34 170 204 34 34
28 37 69 17 17 17 85 102 17 17
28 37 69 17 17 17 85 102 17 17
```
程序很快就能输出的到正确结果。

### Part 4: conclusion

这次的challenge虽然乍一看上去不难，但是若能理解到论文的精髓却不容易。一开始我们都以为B＊A只需要完成一个向量乘矩阵就可以了，然后B矩阵就拆成一列列向量和A乘。但是后来发现，为什么能叫做concurrent matrix multiplication？如果每次都做单步向量矩阵相乘，那么和顺序执行有什么区别，甚至会更慢吧？多读几遍发现，论文精髓在于，把B矩阵抽象成N个输入流，每个输入流代表B矩阵一列的数据，这样B矩阵的行数可以无限长；然后有点类似于流水线似的，上一行和下一行数据乘法是流水似的经过A矩阵。最后结果也是不断输出的N个流，代表着答案矩阵C的N列。这样流水线运转起来，N＊N个中间节点（进程）并行运算，然后通过IPC机制传递数据，实在是巧妙～

为了实现流水线，就需要严格地同步，幸好JOS的IPC机制是能保证一次服务一对进程，并且阻塞式发送接收保证不会打断。综上，在JOS中实现Hoare的算法是可行的，正确性也比较好控制；而且我们也很好地还原了算法，重现了Hoare算法的精妙之处，还把3＊3扩展到N＊N的情况。我们认为此次的challenge做得挺完善，整个机制与系统都比较robust。
