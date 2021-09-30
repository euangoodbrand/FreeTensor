import sys
import time
import itertools
import numpy as np
import ir
import ir.debug


def load_faces(path: str):
    """
    Load a 3D object and returns the adjacency array of the faces


    Parameters
    ----------
    path: str
        Path to a 3D object file, where a `v <x> <y> <z>` line means there is a vertex at coordinate (x, y, z),
        a `f <i> <j> <k>` line means there is a face among vertices i, j and k. Faces are stored in conter-clockwise
        order


    Returns
    -------
    (np.array, np.array)
        ret[0] is an n*3-shaped numpy array, where n is the number of vertices. array[i] = the coordinate (x, y, z)
        ret[1] is an m*3-shaped numpy array, where m is the number of faces. array[i] = each vertices of the face
    """

    vertices = []
    faces = []
    for line in open(path):
        if line.startswith('v'):
            vertices.append(tuple(map(float, line.split()[1:])))
        if line.startswith('f'):
            faces.append(tuple(map(lambda x: int(x) - 1, line.split()[1:])))
    return np.array(vertices, dtype=np.float32), np.array(faces, dtype=np.int32)


jit_cache = {}


def rasterize(vertices, faces, y, h, w, n_verts, n_faces, device, mtype,
              local_mtype):
    """
    Compute soft rasterization of each faces

    Suppose the points are already transposed, so we are viewing inside 0 <= x <= 1 and 0 <= y <= 1, along z-axis.
    The resolution along x and y is h and w, correspondingly.

    Returns
    -------
    jnp.array
        An h*w*m-shaped tensor, where m is the number of faces, tensor[i, j, k] = the probability of face k at
        pixel (i, j)
    """

    if (h, w, n_verts, n_faces) in jit_cache:
        exe = jit_cache[(h, w, n_verts, n_faces)]

    else:

        sigma = 1e-4

        @ir.transform
        def cross_product(v1, v2):
            ir.declare_var(v1, (2,), "float32", "input", local_mtype)
            ir.declare_var(v2, (2,), "float32", "input", local_mtype)
            y = ir.create_var((), "float32", "output", local_mtype)
            y[()] = v1[0] * v2[1] - v1[1] * v2[0]
            return y

        @ir.transform
        def norm(v):
            ir.declare_var(v, (2,), "float32", "input", local_mtype)
            y = ir.create_var((), "float32", "output", local_mtype)
            y[()] = ir.sqrt(v[0] * v[0] + v[1] * v[1])
            return y

        @ir.transform
        def sub(v1, v2):
            ir.declare_var(v1, (2,), "float32", "input", local_mtype)
            ir.declare_var(v2, (2,), "float32", "input", local_mtype)
            y = ir.create_var((2,), "float32", "output", local_mtype)
            y[0] = v1[0] - v2[0]
            y[1] = v1[1] - v2[1]
            return y

        @ir.transform
        def f(vertices, faces, y):
            ir.declare_var(vertices, (n_verts, 3), "float32", "input", mtype)
            ir.declare_var(faces, (n_faces, 3), "int32", "input", mtype)
            ir.declare_var(y, (n_faces, h, w), "float32", "output", mtype)

            "nid: Li"
            for i in range(n_faces):
                v1 = ir.create_var((2,), "float32", "cache", local_mtype)
                v2 = ir.create_var((2,), "float32", "cache", local_mtype)
                v3 = ir.create_var((2,), "float32", "cache", local_mtype)
                v1[0] = vertices[faces[i, 0], 0]
                v1[1] = vertices[faces[i, 0], 1]
                v2[0] = vertices[faces[i, 1], 0]
                v2[1] = vertices[faces[i, 1], 1]
                v3[0] = vertices[faces[i, 2], 0]
                v3[1] = vertices[faces[i, 2], 1]
                "nid: V_e1"
                e1 = sub(v2, v1)
                "nid: V_e2"
                e2 = sub(v3, v2)
                "nid: V_e3"
                e3 = sub(v1, v3)
                "nid: V_len1"
                len1 = norm(e1)
                "nid: V_len2"
                len2 = norm(e2)
                "nid: V_len3"
                len3 = norm(e3)

                "nid: Lj"
                for j in range(h):
                    "nid: Lk"
                    for k in range(w):
                        pixel = ir.create_var((2,), "float32", "cache",
                                              local_mtype)
                        pixel[0] = 1. / (h - 1) * j
                        pixel[1] = 1. / (w - 1) * k

                        p1 = sub(pixel, v1)
                        p2 = sub(pixel, v2)
                        p3 = sub(pixel, v3)
                        cp1 = cross_product(p1, e1)
                        cp2 = cross_product(p2, e2)
                        cp3 = cross_product(p3, e3)
                        "nid: V_dist1"
                        dist1 = norm(p1)
                        "nid: V_dist2"
                        dist2 = norm(p2)
                        "nid: V_dist3"
                        dist3 = norm(p3)

                        dist = ir.create_var((), "float32", "cache",
                                             local_mtype)
                        dist[()] = ir.min(
                            ir.min(
                                ir.min(
                                    ir.abs(cp1[()]) / len1[()],
                                    ir.abs(cp2[()]) / len2[()]),
                                ir.abs(cp3[()]) / len3[()]),
                            ir.min(ir.min(dist1[()], dist2[()]), dist3[()]))

                        y[i, j, k] = ir.if_then_else(
                            cp1[()] < 0 and cp2[()] < 0 and cp3[()] < 0, 1,
                            -1) * dist[()] * dist[()] / sigma

        s = ir.Schedule(f)
        if device.target().type() == ir.TargetType.CPU:
            s.parallelize('Li', 'openmp')
            s.inline('V_dist1:y')
            s.inline('V_dist2:y')
            s.inline('V_dist3:y')
        else:
            s.inline('V_e1:y')
            s.inline('V_e2:y')
            s.inline('V_e3:y')
            s.inline('V_len1:y')
            s.inline('V_len2:y')
            s.inline('V_len3:y')
            s.inline('V_dist1:y')
            s.inline('V_dist2:y')
            s.inline('V_dist3:y')
            s.inline(':dist')
            Ljk = s.merge('Lj', 'Lk')
            serial, thr = s.split(Ljk, 1024)
            s.parallelize(thr, 'threadIdx.x')
            s.set_mem_type(":v1", "gpu/shared")
            s.set_mem_type(":v2", "gpu/shared")
            s.set_mem_type(":v3", "gpu/shared")
            s.parallelize('Li', 'blockIdx.x')
        f = ir.lower(s.func(), device.target())
        print(f)
        code = ir.codegen(f, device.target())
        print(ir.debug.with_line_no(code))
        exe = ir.Driver(f, code, device)
        exe.set_params(vertices, faces, y)
        # TODO: do not set_params here
        jit_cache[(h, w, n_verts, n_faces)] = exe

    exe.run()
    exe.sync()


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <cpu/gpu> <obj-file>")
        exit(-1)
    device = sys.argv[1]
    obj_file = sys.argv[2]

    vertices, faces = load_faces(obj_file)
    n_verts = vertices.shape[0]
    n_faces = faces.shape[0]
    h = 64
    w = 64
    y = np.zeros((n_faces, h, w), dtype="float32")

    if device == 'gpu':
        ir_dev = ir.Device(ir.GPU())
        ir_mtype = 'gpu/global'
        ir_local_mtype = 'gpu/local'
    else:
        assert device == 'cpu'
        ir_dev = ir.Device(ir.CPU())
        ir_mtype = 'cpu'
        ir_local_mtype = 'cpu'

    vertices = ir.Array(vertices, ir_dev)
    faces = ir.Array(faces, ir_dev)
    y = ir.Array(y, ir_dev)

    test_num = 100
    rasterize(vertices, faces, y, h, w, n_verts, n_faces, ir_dev, ir_mtype,
              ir_local_mtype)  # init lazy ops
    t0 = time.time()
    for i in range(test_num):
        rasterize(vertices, faces, y, h, w, n_verts, n_faces, ir_dev, ir_mtype,
                  ir_local_mtype)
    t1 = time.time()

    print(f"Time = {(t1 - t0) / test_num * 1000} ms")
