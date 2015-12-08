#include <inc/lib.h>

#define NORDER 10
#define NDIMENSION (NORDER + 2)
#define MAXLENQUEUE (NORDER)

#define PROCMASK 0xFFFF0000
#define PROCSTART 0xAB010000
#define PROCEND 0xABFF0000

// #define debug

#ifdef debug
    #define dbg_cprintf cprintf
#else
    #define dbg_cprintf
#endif

/*
 * envid, A矩阵，定义见Hoare论文
 */
envid_t env_id_mat[NDIMENSION][NDIMENSION];
// int A[NORDER][NORDER] = {
//     {1, 5, 6},
//     {2, 5, 7},
//     {2, 1, 4}
// };
// int B[NORDER][NORDER] = {
//     {1, 2, 7},
//     {4, 5, 1},
//     {3, 8, 9}
// };
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
int C[NORDER][NORDER];

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

/*
int
example(void) {
    dbg_cprintf("i am master environment %08x\n", thisenv->env_id);
    envid_t id = fork();
    if (id == 0) {
        dbg_cprintf("i am child  environment %08x\n", thisenv->env_id);
        struct RequiredData rd[2] = {
            {0x00001001, 0, 0},
            {0x00001001, 0, 0}
        };
        wait_for_data(2, rd);
        dbg_cprintf("rd[0] = %d\n", rd[0].value);
        dbg_cprintf("rd[1] = %d\n", rd[1].value);
        return 0;
    }
    ipc_send(id, 1, 0, 0);
    ipc_send(id, 2, 0, 0);
    return 0;
}
*/

/*
 * 队列数据结构
 */
struct Queue {
    int value[MAXLENQUEUE];
    size_t s, t;
};

void
q_init(struct Queue* q) {
    q->s = 0;
    q->t = 0;
}

int
q_push(struct Queue* q, int v) {
    if (q->t >= MAXLENQUEUE)
        panic("q_push: out of range!");
    q->value[q->t] = v;
    q->t++;
    return 0;
}

int
q_empty(struct Queue* q) {
    if (q->s >= q->t)
        return 1;
    else
        return 0;
}

int
q_top(struct Queue* q) {
    if (q_empty(q))
        panic("q_top: empty queue!");
    return q->value[q->s];
}

int
q_pop(struct Queue* q) {
    if (q_empty(q))
        panic("q_pop: empty queue!");
    q->s++;
    return 0;
}

/*
 * 5种进程模型，作用参考Hoare的论文。
 */
int
north_proc(void) {
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
        dbg_cprintf("north_proc(%d, %d) %08x: started successfully.\n", x, y, thisenv->env_id);
    }
    else
        panic("north_proc: unexpected recv: %08x got %x from %08x\n", sys_getenvid(), value, who);

    // 进程主体
    while (count < NORDER) {
        dbg_cprintf("north_proc(%d, %d) %08x: sending %d to center_proc(%d, %d) %08x.\n", x, y, thisenv->env_id, 0, x + 1, y, env_id_mat[x + 1][y]);
        ipc_send(env_id_mat[x + 1][y], 0, 0, 0);
        count++;
    }
    dbg_cprintf("north_proc(%d, %d) %08x: return successfully.\n", x, y, thisenv->env_id);
    return 0;
}

int
east_proc(void) {
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
        dbg_cprintf("east_proc(%d, %d) %08x: started successfully.\n", x, y, thisenv->env_id);
    }
    else
        panic("east_proc: unexpected recv: %x got %x from %x\n", sys_getenvid(), value, who);

    // 进程设置
    // 设置需求数据来源
    from[0] = env_id_mat[x][y - 1];

    // 进程主体
    while (count < NORDER) {
        // 检查数据的身份
        value = ipc_recv(&who, 0, 0);
        for (i = 0; i < NDATA; i++)
            if (from[i] == who)
                break;
        if (i == NDATA)
            panic("east_proc: unexpected recv: %08x got %x from %08x\n", sys_getenvid(), value, who);
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
            dbg_cprintf("east_proc(%d, %d) %08x: nothing to do.\n", x, y, thisenv->env_id);
            count++;
        }
    }
    dbg_cprintf("east_proc(%d, %d) %08x: return successfully.\n", x, y, thisenv->env_id);
    return 0;
}

int
south_proc(void) {
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
        dbg_cprintf("south_proc(%d, %d) %08x: started successfully.\n", x, y, thisenv->env_id);
    }
    else
        panic("south_proc: unexpected recv: %x got %x from %x\n", sys_getenvid(), value, who);

    // 进程设置
    // 设置需求数据来源
    from[0] = env_id_mat[x - 1][y];

    // 进程主体
    while (count < NORDER) {
        // 检查数据的身份
        value = ipc_recv(&who, 0, 0);
        for (i = 0; i < NDATA; i++)
            if (from[i] == who)
                break;
        if (i == NDATA)
            panic("south_proc: unexpected recv: %08x got %x from %08x\n", sys_getenvid(), value, who);
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
            dbg_cprintf("south_proc(%d, %d) %08x: sending %d to collector(%d, %d) %08x.\n", x, y, thisenv->env_id, data[0], NDIMENSION - 1, NDIMENSION - 1, env_id_mat[NDIMENSION - 1][NDIMENSION - 1]);
            ipc_send(env_id_mat[NDIMENSION - 1][NDIMENSION - 1], data[0], 0, 0);
            count++;
        }
    }
    dbg_cprintf("south_proc(%d, %d) %08x: return successfully.\n", x, y, thisenv->env_id);
    return 0;
}

int
west_proc(void) {
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
        dbg_cprintf("west_proc(%d, %d) %08x: started successfully.\n", x, y, thisenv->env_id);
    }
    else
        panic("west_proc: unexpected recv: %x got %d from %x\n", sys_getenvid(), value, who);

    // 进程设置
    // 设置需求数据来源
    from[0] = env_id_mat[0][0];

    // 进程主体
    while (count < NORDER) {
        // 检查数据的身份
        value = ipc_recv(&who, 0, 0);
        for (i = 0; i < NDATA; i++)
            if (from[i] == who)
                break;
        if (i == NDATA)
            panic("west_proc: unexpected recv: %08x got %x from %08x\n", sys_getenvid(), value, who);
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
            dbg_cprintf("west_proc(%d, %d) %08x: sending %d to center_proc(%d, %d) %08x.\n", x, y, thisenv->env_id, data[0], x, y + 1, env_id_mat[x][y + 1]);
            ipc_send(env_id_mat[x][y + 1], data[0], 0, 0);
            count++;
        }
    }
    dbg_cprintf("west_proc(%d, %d) %08x: return successfully.\n", x, y, thisenv->env_id);
    return 0;
}

int
center_proc(void) {
    // 数据定义部分
    const int NDATA = 2;    // 需求数据数目
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
        dbg_cprintf("center_proc(%d, %d) %08x: started successfully.\n", x, y, thisenv->env_id);
    }
    else
        panic("center_proc: unexpected recv: %x got %x from %x\n", sys_getenvid(), value, who);

    // 进程设置
    // 设置需求数据来源
    from[0] = env_id_mat[x - 1][y];
    from[1] = env_id_mat[x][y - 1];

    // 进程主体
    while (count < NORDER) {
        // 检查数据的身份
        value = ipc_recv(&who, 0, 0);
        for (i = 0; i < NDATA; i++)
            if (from[i] == who)
                break;
        if (i == NDATA)
            panic("center_proc: unexpected recv: %08x got %x from %08x\n", sys_getenvid(), value, who);
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
            value = A[x - 1][y - 1] * data[1] + data[0];
            dbg_cprintf("center_proc(%d, %d) %08x: sending %d to proc(%d, %d) %08x.\n", x, y, thisenv->env_id, value, x + 1, y, env_id_mat[x + 1][y]);
            ipc_send(env_id_mat[x + 1][y], value, 0, 0);
            dbg_cprintf("center_proc(%d, %d) %08x: sending %d to proc(%d, %d) %08x.\n", x, y, thisenv->env_id, data[1], x, y + 1, env_id_mat[x][y + 1]);
            ipc_send(env_id_mat[x][y + 1], data[1], 0, 0);
            count++;
        }
    }
    dbg_cprintf("center_proc(%d, %d) %08x: return successfully.\n", x, y, thisenv->env_id);
    return 0;
}

/*
 * 结果收集进程
 */
void
collector(void) {
    // 数据定义部分
    const int NDATA = 2;    // 需求数据数目
    int i, j;               // 循环变量
    int x, y;               // 当前proc的env_id在env_id_mat中的位置
    int value, count = 0;   // 数据和获得数据计数
    envid_t who;            // 信息来源
    envid_t from[NDATA];    // 需求数据来源
    int data[NDATA];        // 一轮中收集的数据
    struct Queue queue[NDATA];  // 数据队列
    int ready = 1;          // 是否收集了一轮所需的数据
    int index[NORDER] = {0};

    // 进程初始化
    // 从master获得PROCSTART信号，并设置x, y
    value = ipc_recv(&who, 0, 0);
    if (who == env_id_mat[0][0] && (value & PROCMASK) == PROCSTART) {
        x = (value & 0xFF00) >> 8;
        y = (value & 0XFF);
        dbg_cprintf("collector(%d, %d) %08x: started successfully.\n", x, y, thisenv->env_id);
    }
    else
        panic("collector: unexpected recv: %x got %x from %x\n", sys_getenvid(), value, who);

    // 进程主体
    dbg_cprintf("collector(%d, %d) %08x: collecting results.\n", x, y, thisenv->env_id);
    while (count < NORDER * NORDER) {
        value = ipc_recv(&who, 0, 0);
        for (i = 1; i <= NORDER; i++)
            if (who == env_id_mat[NDIMENSION - 1][i])
                break;
        if (i == NORDER + 1)
            panic("collector: unexpected recv: %08x got %x from %08x\n", sys_getenvid(), value, who);
        C[index[i-1]++][i-1] = value; // receive from different stage stream!!区分不同阶段计算出来的值
        count++;
    }

    for (i = 0; i < NORDER; i++) {
        for (j = 0; j < NORDER; j++)
            cprintf("%d ", C[i][j]);
        cprintf("\n");
    }

    value = PROCEND;
    dbg_cprintf("collector(%d, %d) %08x: sending %d to master(%d, %d) %08x.\n", x, y, thisenv->env_id, value, 0, 0, env_id_mat[0][0]);
    ipc_send(env_id_mat[0][0], value, 0, 0);
    dbg_cprintf("collector(%d, %d) %08x: return successfully.\n", x, y, thisenv->env_id);
}

/*
 * 主控制进程
 */
void
master(void) {
    int i, j;
    int value, count = 0;
    envid_t env_id = 0x00001001, who;
    env_id_mat[0][0] = thisenv->env_id;

    // env_id_mat需要被提前设置，否则后面fork出的程序无法得到其信息
    for (i = 1; i <= NORDER; i++) env_id_mat[NDIMENSION - 1][i] = ++env_id;
    for (i = 1; i <= NORDER; i++) env_id_mat[i][NDIMENSION - 1] = ++env_id;
    for (i = NORDER; i >=1; i--) for (j = NORDER; j >=1; j--) env_id_mat[i][j] = ++env_id;
    for (i = 1; i <= NORDER; i++) env_id_mat[0][i] = ++env_id;
    for (i = 1; i <= NORDER; i++) env_id_mat[i][0] = ++env_id;
    env_id_mat[NDIMENSION - 1][NDIMENSION - 1] = ++env_id;

    for (i = 1; i <= NORDER; i++) {
        envid_t id = fork();
        if (id == 0) {
            south_proc();
            return;
        }
        if (id != env_id_mat[NDIMENSION - 1][i])
            panic("unmatched env id: proc(%d, %d) %08x.", NDIMENSION - 1, i, id);
    }
    for (i = 1; i <= NORDER; i++) {
        envid_t id = fork();
        if (id == 0) {
            east_proc();
            return;
        }
        if (id != env_id_mat[i][NDIMENSION - 1])
            panic("unmatched env id: proc(%d, %d) %08x.", i, NDIMENSION - 1, id);
    }
    for (i = NORDER; i >=1; i--)
        for (j = NORDER; j >=1; j--) {
            envid_t id = fork();
            if (id == 0) {
                center_proc();
                return;
            }
            if (id != env_id_mat[i][j])
                panic("unmatched env id: proc(%d, %d) %08x.", i, j, id);
        }
    for (i = 1; i <= NORDER; i++) {
        envid_t id = fork();
        if (id == 0) {
            north_proc();
            return;
        }
        if (id != env_id_mat[0][i])
            panic("unmatched env id: proc(%d, %d) %08x.", 0, i, id);
    }
    for (i = 1; i <= NORDER; i++) {
        envid_t id = fork();
        if (id == 0) {
            west_proc();
            return;
        }
        if (id != env_id_mat[i][0])
            panic("unmatched env id: proc(%d, %d) %08x.", i, 0, id);
    }
    envid_t id = fork();
    if (id == 0) {
        collector();
        return;
    }
    if (id != env_id_mat[NDIMENSION - 1][NDIMENSION - 1])
        panic("unmatched env id: proc(%d, %d) %08x.", NDIMENSION - 1, NDIMENSION - 1, id);

    // 启动所有进程
    // 要严格按照这个启动顺序，否则会panic
    for (i = 1; i <= NORDER; i++) {
        value = PROCSTART | (NDIMENSION - 1) << 8 | i;
        ipc_send(env_id_mat[NDIMENSION - 1][i], value, 0, 0);
    }
    for (i = 1; i <= NORDER; i++) {
        value = PROCSTART | i << 8 | (NDIMENSION - 1);
        ipc_send(env_id_mat[i][NDIMENSION - 1], value, 0, 0);
    }
    for (i = 1; i <= NORDER; i++)
        for (j = 1; j <= NORDER; j++) {
            value = PROCSTART | i << 8 | j;
            ipc_send(env_id_mat[i][j], value, 0, 0);
        }
    for (i = 1; i <= NORDER; i++) {
        value = PROCSTART | 0 << 8 | i;
        ipc_send(env_id_mat[0][i], value, 0, 0);
    }
    for (i = 1; i <= NORDER; i++) {
        value = PROCSTART | i << 8 | 0;
        ipc_send(env_id_mat[i][0], value, 0, 0);
    }
    value = PROCSTART | (NDIMENSION - 1) << 8 | (NDIMENSION - 1);
    ipc_send(env_id_mat[NDIMENSION - 1][NDIMENSION - 1], value, 0, 0);

    for (i = 1; i <= NORDER; i++)
        for (j = 1; j <= NORDER; j++) {
            ipc_send(env_id_mat[j][0], B[i-1][j-1], 0, 0);
        }

    value = ipc_recv(&who, 0, 0);
    if ((who != env_id_mat[NDIMENSION - 1][NDIMENSION - 1]) || ((value & PROCMASK) != PROCEND))
        panic("master: unexpected recv: %x got %x from %x\n", sys_getenvid(), value, who);
    dbg_cprintf("master(%d, %d) %08x: return successfully.\n", 0, 0, thisenv->env_id);
}

void
umain(int argc, char **argv) {
    master();
}
