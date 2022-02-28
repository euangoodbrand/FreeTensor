using CUDA
using IterTools
# using OMEinsum
using Flux, Zygote

include("../../common/julia/io.jl")

function dilated_attention(q, k, v, w, dilation)::CuArray{Float32}
    feat_len, seq_len, n_heads = size(q)
    sqrt_d = sqrt(feat_len)

    # pad_arr1 = zeros(feat_len, w * dilation, n_heads)
    # pad_arr2 = zeros(feat_len, w * dilation, n_heads)
    # pad_arr3 = zeros(feat_len, w * dilation, n_heads)
    # pad_arr4 = zeros(feat_len, w * dilation, n_heads)
    # pad_arr1 = CuArray{Float32}(undef, (feat_len, w * dilation, n_heads))
    # pad_arr2 = CuArray{Float32}(undef, (feat_len, w * dilation, n_heads))
    # pad_arr3 = CuArray{Float32}(undef, (feat_len, w * dilation, n_heads))
    # pad_arr4 = CuArray{Float32}(undef, (feat_len, w * dilation, n_heads))
    # pad_k = cat(pad_arr1, k, pad_arr2, dims=2)
    # pad_v = cat(pad_arr3, v, pad_arr4, dims=2)
    pad_k = pad_zeros(k, (0, w * dilation, 0))
    pad_v = pad_zeros(v, (0, w * dilation, 0))

    indexes = map(i -> i[2] + i[1] * dilation + i[3] * (seq_len + 2 * w * dilation), product(0:2*w, 1:seq_len, 0:n_heads-1))
    diag_k = view(reshape(pad_k, (feat_len, :)), :, indexes) # (feat_len, 2*w+1, seq_len, n_heads)
    diag_v = view(reshape(pad_v, (feat_len, :)), :, indexes)

    # return CuArray(dropdims(sum(diag_k, dims=2), dims=2)) # test

    # attn = cu(ein"pji,pkji->kji"(q, diag_k))  :   (2 * w + 1, seq_len, n_heads)
    attn = dropdims(sum(broadcast(*,
        reshape(q, (feat_len, 1, seq_len, n_heads)), diag_k
        ), dims=1), dims=1)
    # attn = dropdims(sum(diag_k, dims=1), dims=1)
    attn = softmax(attn, dims=1) ./ sqrt_d
    # y = cu(ein"kji,pkji->pji"(attn, diag_v))  :   (feat_len, seq_len, n_heads)
    y = dropdims(sum(broadcast(*,
        reshape(attn, (1, 2 * w + 1, seq_len, n_heads)), diag_v
        ), dims=2), dims=2)
    # y = dropdims(sum(diag_v, dims=2), dims=2)
    return y
end

function transformer(Q, K, V, w, dilation, dilation_heads)::CuArray{Float32}
    # return dilated_attention(Q, K, V, w, dilation)
    front_heads = dilated_attention(Q[:, :, begin:dilation_heads], K[:, :, begin:dilation_heads],
                                    V[:, :, begin:dilation_heads], w, dilation)
    back_heads = dilated_attention(Q[:, :, dilation_heads+1:end], K[:, :, dilation_heads+1:end],
                                   V[:, :, dilation_heads+1:end], w, 1)
    return cat(front_heads, back_heads, dims=3)
end

function main()
    if length(ARGS) != 2
        println("Usage: " * PROGRAM_FILE * "  Inf/For/Bac")
        exit(-1)
    end

    n_heads = 8
    seq_len = 10000
    feat_len = 512
    w = 32
    dilation = 4  # counts from 1
    dilation_heads = 2
    q = read_vec("../q.in", "Float32")
    k = read_vec("../k.in", "Float32")
    v = read_vec("../v.in", "Float32")
    # q = reshape(readdlm(open("../q.in"), Float32), (feat_len, seq_len, n_heads))
    # k = reshape(readdlm(open("../k.in"), Float32), (feat_len, seq_len, n_heads))
    # v = reshape(readdlm(open("../v.in"), Float32), (feat_len, seq_len, n_heads))
    y = zeros(Float32, (feat_len, seq_len, n_heads))
    d_y = read_vec("../d_y.in", "Float32")
    # d_y = reshape(readdlm(open("../d_y.in"), Float32), (feat_len, seq_len, n_heads))

    q = CuArray(q)
    k = CuArray(k)
    v = CuArray(v)
    d_y = CuArray(d_y)

    warmup_num = 1
    test_num = 0

    if ARGS[2] == "Inf"
        for i = 1:warmup_num
            y = transformer(q, k, v, w, dilation, dilation_heads)
            println("warmup: [" * string(i) * "/" * string(warmup_num) * "]  Done.")
            if i == 1
                write_vec("y.out", Array(y))
                # writedlm("y.out", [@sprintf("%.10f", i) for i in reshape(Array(y), (1, :))], ' ')
            end
        end
        time = @timed begin
            for i = 1:test_num
                y = transformer(q, k, v, w, dilation, dilation_heads)
                println("test: [" * string(i) * "/" * string(test_num) * "]  Done.")
            end
        end
        println("Inference Time = " * string(time.time / test_num * 1000) * " ms")
    elseif ARGS[2] == "For"
        println("Compiling Forward...")
        for i = 1:warmup_num
            z, back = Zygote.pullback(
                (q, k, v) -> sum(transformer(q, k, v, w, dilation, dilation_heads) .* d_y),
                q, k, v
            )
            println("warmup: [" * string(i) * "/" * string(warmup_num) * "]  Done.")
        end
        time = @timed begin
            for i = 1:test_num
                z, back = Zygote.pullback(
                    (q, k, v) -> sum(transformer(q, k, v, w, dilation, dilation_heads) .* d_y),
                    q, k, v
                )
                println("test: [" * string(i) * "/" * string(test_num) * "]  Done.")
            end
        end
        println("Forward Time = " * string(time.time / test_num * 1000) * " ms")
    elseif ARGS[2] == "Bac"
        z, back = Zygote.pullback(
            (q, k, v) -> sum(transformer(q, k, v, w, dilation, dilation_heads) .* d_y),
            q, k, v
        )
        for i = 1:warmup_num
            back_array = back(1)
            println("warmup: [" * string(i) * "/" * string(warmup_num) * "]  Done.")
            if i == 1
                write_vec("d_q.out", back_array[1])
                write_vec("d_k.out", back_array[2])
                write_vec("d_v.out", back_array[3])
            end
        end
        time = @timed begin
            for i = 1:test_num
                back(1)
                println("test: [" * string(i) * "/" * string(test_num) * "]  Done.")
            end
        end
        println("Forward Time = " * string(time.time / test_num * 1000) * " ms")
    else
        println("Usage: " * PROGRAM_FILE * "Inf/For/Bac")
        exit(-1)
    end
end

main()
