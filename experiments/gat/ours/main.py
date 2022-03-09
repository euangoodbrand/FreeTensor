import sys
import time
import itertools
import argparse
import numpy as np
import ir
from ir.libop import *
import ir.debug

sys.path.append('../..')
from common.numpy.io import load_txt, store_txt


def load_data(data_name: str):
    '''
    Load data from ../data/{data_name}.config and ../data/{data_name}.graph

    Returns (#vertices, #edges, ptr, idx), where ptr and idx forms a CSR format
    '''

    with open(f"../data/{data_name}.config", 'r') as f:
        num_v, num_e = map(int, f.readline().strip().split(' '))

    with open(f"../data/{data_name}.graph", 'r') as f:
        ptr = np.array(list(map(int, f.readline().strip().split(" "))))
        idx = np.array(list(map(int, f.readline().strip().split(" "))))

    return num_v, num_e, ptr, idx


def compile_all(num_v, num_e, feat_len, device):
    mtype = device.main_mem_type()

    inf = float("inf")

    @ir.transform
    def inference(ptr, idx, feat, weight, attn_l, attn_r, y):
        ir.declare_var(ptr, (num_v + 1,), "int32", "input", mtype)
        ir.declare_var(idx, (num_e,), "int32", "input", mtype)
        ir.declare_var(feat, (num_v, feat_len), "float32", "input", mtype)
        ir.declare_var(weight, (feat_len, feat_len), "float32", "input", mtype)
        ir.declare_var(attn_l, (feat_len,), "float32", "input", mtype)
        ir.declare_var(attn_r, (feat_len,), "float32", "input", mtype)
        ir.declare_var(y, (num_v, feat_len), "float32", "output", mtype)

        feat2 = matmul(feat, weight)
        att_l = matmul(feat2, attn_l)
        att_r = matmul(feat2, attn_r)

        edge = ir.create_var((num_e,), "float32", mtype)
        edge_exp = ir.create_var((num_e,), "float32", mtype)
        'nid: Li'
        'no_deps: edge'
        'no_deps: edge_exp'
        'no_deps: idx'
        for i in range(num_v):
            edge_max = ir.create_var((), "float32", mtype)
            edge_max[()] = -inf
            'nid: Lk1'
            'no_deps: att_l'
            for k in range(ptr[i], ptr[i + 1]):
                e = ir.create_var((), "float32", mtype)
                e[()] = att_l[idx[k]] + att_r[i]
                edge[k] = ir.if_then_else(e[()] >= 0, e[()], e[()] * 0.1)
                edge_max[()] = ir.max(edge_max[()], edge[k])
            edge_sum = ir.create_var((), "float32", mtype)
            edge_sum[()] = 0
            'nid: Lk2'
            for k in range(ptr[i], ptr[i + 1]):
                edge_exp[k] = ir.exp(edge[k] - edge_max[()])
                edge_sum[()] += edge_exp[k]
            'nid: Lj'
            for j in range(feat_len):
                y[i, j] = 0
                'nid: Lk3'
                'no_deps: feat2'
                for k in range(ptr[i], ptr[i + 1]):
                    y[i, j] += feat2[idx[k], j] * edge_exp[k] / edge_sum[()]

    forward, backward, requires, privdes, _ = ir.grad(
        inference, set(["feat", "weight", "attn_l", "attn_r"]), set(["y"]))

    print("# Inference:")
    print(inference)
    t0 = time.time()
    s = ir.Schedule(inference)
    s.auto_schedule(device.target())
    f = ir.lower(s.func(), device.target())
    code = ir.codegen(f, device.target())
    inference_exe = ir.Driver(inference, code, device)
    t1 = time.time()
    print(f)
    print(ir.debug.with_line_no(code))
    print(f"Inference compiling time: {t1 - t0}s")

    return inference_exe, None, None
    #print("# Forward:")
    #print(forward)
    #s = ir.Schedule(forward)
    #s.auto_schedule(device.target())
    #f = ir.lower(s.func(), device.target())
    #print(f)
    #code = ir.codegen(f, device.target())
    #print(ir.debug.with_line_no(code))
    #forward_exe = ir.Driver(forward, code, device)

    #print("# Backward:")
    #print(backward)
    #s = ir.Schedule(backward)
    #s.auto_schedule(device.target())
    #print(s.ast())
    #f = ir.lower(s.func(), device.target())
    #print(f)
    #code = ir.codegen(f, device.target())
    #print(ir.debug.with_line_no(code))
    #backward_exe = ir.Driver(backward, code, device)

    #def run_backward(ptr, idx, x, w, w_attn_1, w_attn_2, y, d_y, d_x, d_w,
    #                 d_w_attn_1, d_w_attn_2):
    #    kvs = {}
    #    kvs[privdes['y']] = d_y
    #    kvs[requires['feat']] = d_x
    #    kvs[requires['weight']] = d_w
    #    kvs[requires['attn_l']] = d_w_attn_1
    #    kvs[requires['attn_r']] = d_w_attn_2
    #    backward_exe(ptr, idx, x, w, w_attn_1, w_attn_2, y, **kvs)

    #return inference_exe, forward_exe, run_backward


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('target', nargs='?')
    parser.add_argument('--warmup-repeat',
                        type=int,
                        default=10,
                        dest='warmup_num')
    parser.add_argument('--timing-repeat',
                        type=int,
                        default=100,
                        dest='test_num')
    parser.add_argument('--profile-gpu',
                        action='store_true',
                        dest='profile_gpu')
    cmd_args = parser.parse_args()

    if cmd_args.profile_gpu:
        from common.gpu import profile_start, profile_stop

    device = cmd_args.target

    ptr = load_txt("../ptr.in", "int32")
    idx = load_txt("../idx.in", "int32")
    num_v = ptr.shape[0] - 1
    num_e = idx.shape[0]

    feat_len = 32
    ptr = ptr.astype("int32")
    idx = idx.astype("int32")
    x = load_txt("../x.in", "float32")
    w = load_txt("../w.in", "float32")
    w_attn_1 = load_txt("../w_attn_1.in", "float32")
    w_attn_2 = load_txt("../w_attn_2.in", "float32")
    y = np.zeros((num_v, feat_len), dtype="float32")
    d_x = np.zeros(x.shape, dtype='float32')
    d_w = np.zeros(w.shape, dtype='float32')
    d_w_attn_1 = np.zeros(w_attn_1.shape, dtype='float32')
    d_w_attn_2 = np.zeros(w_attn_2.shape, dtype='float32')
    d_y = load_txt("../d_y.in", "float32")

    if device == 'gpu':
        ir_dev = ir.Device(ir.GPU())
    else:
        assert device == 'cpu'
        ir_dev = ir.Device(ir.CPU())

    ptr = ir.Array(ptr, ir_dev)
    idx = ir.Array(idx, ir_dev)
    x = ir.Array(x, ir_dev)
    w = ir.Array(w, ir_dev)
    w_attn_1 = ir.Array(w_attn_1, ir_dev)
    w_attn_2 = ir.Array(w_attn_2, ir_dev)
    y = ir.Array(y, ir_dev)
    d_x = ir.Array(d_x, ir_dev)
    d_w = ir.Array(d_w, ir_dev)
    d_w_attn_1 = ir.Array(d_w_attn_1, ir_dev)
    d_w_attn_2 = ir.Array(d_w_attn_2, ir_dev)
    d_y = ir.Array(d_y, ir_dev)

    inference, forward, backward = compile_all(num_v, num_e, feat_len, ir_dev)

    print(
        f"{cmd_args.warmup_num} warmup, {cmd_args.test_num} repeats for evalution"
    )
    warmup_num = cmd_args.warmup_num
    test_num = cmd_args.test_num

    for i in range(warmup_num):
        inference(ptr, idx, x, w, w_attn_1, w_attn_2, y)
        if i == 0:
            store_txt("y.out", y.numpy().reshape((num_v, feat_len)))
    ir_dev.sync()
    if cmd_args.profile_gpu:
        profile_start()
    t0 = time.time()
    for i in range(test_num):
        inference(ptr, idx, x, w, w_attn_1, w_attn_2, y)
    ir_dev.sync()
    t1 = time.time()
    if cmd_args.profile_gpu:
        profile_stop()

    print(f"Inference Time = {(t1 - t0) / test_num * 1000} ms")

    if cmd_args.profile_gpu:
        exit(0)

    #for i in range(warmup_num):
    #    forward(ptr, idx, x, w, w_attn_1, w_attn_2, y)
    #ir_dev.sync()
    #t0 = time.time()
    #for i in range(test_num):
    #    forward(ptr, idx, x, w, w_attn_1, w_attn_2, y)
    #ir_dev.sync()
    #t1 = time.time()

    #print(f"Forward Time = {(t1 - t0) / test_num * 1000} ms")

    #for i in range(warmup_num):
    #    backward(ptr, idx, x, w, w_attn_1, w_attn_2, y, d_y, d_x, d_w,
    #             d_w_attn_1, d_w_attn_2)
    #    if i == 0:
    #        store_txt("d_x.out", d_x.numpy().reshape((num_v, feat_len)))
    #        store_txt("d_w.out", d_w.numpy().reshape((feat_len, feat_len)))
    #        store_txt("d_w_attn_1.out", d_w_attn_1.numpy())
    #        store_txt("d_w_attn_2.out", d_w_attn_2.numpy())
    #ir_dev.sync()
    #t0 = time.time()
    #for i in range(test_num):
    #    backward(ptr, idx, x, w, w_attn_1, w_attn_2, y, d_y, d_x, d_w,
    #             d_w_attn_1, d_w_attn_2)
    #ir_dev.sync()
    #t1 = time.time()

    #print(f"Backward Time = {(t1 - t0) / test_num * 1000} ms")
