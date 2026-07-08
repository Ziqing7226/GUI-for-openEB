#!/usr/bin/env python3
"""models/convert_to_onnx.py — 将 rpg_e2vid 的 .pth.tar 模型转换为 ONNX 格式。

用法:
    .venv/bin/python models/convert_to_onnx.py \
        --input models/E2VID_lightweight.pth.tar \
        --output models/e2vid_lightweight.onnx

导出的 ONNX 模型支持动态 H/W（适配任意传感器尺寸），并保留
E2VIDRecurrent 的 ConvLTM 循环状态输入/输出（与 C++ 端
e2vid_inference.h 的 N 输入 / M 输出处理对齐）。

依赖: torch (CPU)、onnx、numpy（安装于 .venv）。
参考: ref/rpg_e2vid/{model/unet.py, model/model.py, utils/loading_utils.py}
"""

import argparse
import os
import sys

import numpy as np
import torch
import onnx
import onnx.checker


# ---------------------------------------------------------------------------
# 模型加载（移植自 ref/rpg_e2vid/utils/loading_utils.py）
# ---------------------------------------------------------------------------

def load_model(path_to_model, rpg_e2vid_dir):
    """加载 rpg_e2vid 的 .pth.tar 检查点并实例化模型。

    与 loading_utils.load_model 等价，但显式使用 weights_only=False
    （PyTorch >= 2.6 默认 weights_only=True 会拒绝加载 arch 字符串）。
    """
    sys.path.insert(0, rpg_e2vid_dir)
    from model.model import E2VID, E2VIDRecurrent  # noqa: E402

    print(f"Loading model {path_to_model} ...")
    raw_model = torch.load(path_to_model, map_location="cpu", weights_only=False)

    arch = raw_model["arch"]
    config = raw_model["model"]
    print(f"  arch={arch}, config={config}")

    model_cls = {"E2VID": E2VID, "E2VIDRecurrent": E2VIDRecurrent}[arch]
    model = model_cls(config)
    model.load_state_dict(raw_model["state_dict"])
    model.eval()
    return model


# ---------------------------------------------------------------------------
# ONNX 导出包装器
# ---------------------------------------------------------------------------

class E2VIDRecurrentONNXWrapper(torch.nn.Module):
    """将 E2VIDRecurrent 的 (hidden, cell) 元组状态展平为独立张量。

    E2VIDRecurrent.forward(event_tensor, prev_states) 的 prev_states 是
    [(h0, c0), (h1, c1), (h2, c2)] 列表。ONNX 无法直接导出嵌套元组
    输入，因此本包装器将 6 个状态张量作为独立参数接收，返回时同样展平。

    适用于 num_encoders=3 的 lightweight 模型。若需适配其他 num_encoders，
    按相同模式增减 h/c 参数即可。
    """

    def __init__(self, model):
        super().__init__()
        self.model = model

    def forward(self, event_tensor, h0, c0, h1, c1, h2, c2):
        prev_states = [(h0, c0), (h1, c1), (h2, c2)]
        img, new_states = self.model(event_tensor, prev_states)
        # new_states = [(h0', c0'), (h1', c1'), (h2', c2')]
        return (
            img,
            new_states[0][0], new_states[0][1],
            new_states[1][0], new_states[1][1],
            new_states[2][0], new_states[2][1],
        )


class E2VIDONNXWrapper(torch.nn.Module):
    """非循环 E2VID（plain UNet）的包装器，无状态输入。"""

    def __init__(self, model):
        super().__init__()
        self.model = model

    def forward(self, event_tensor):
        img, _ = self.model(event_tensor, None)
        return img


# ---------------------------------------------------------------------------
# 导出
# ---------------------------------------------------------------------------

def export_recurrent(model, output_path, opset=17):
    """导出 E2VIDRecurrent 模型（含 ConvLSTM 循环状态）。"""
    num_encoders = model.num_encoders
    num_bins = model.num_bins
    base_ch = model.base_num_channels
    if num_encoders != 3:
        raise ValueError(
            f"本脚本目前仅支持 num_encoders=3 的模型（当前={num_encoders}）。"
            "如需其他配置，请修改 E2VIDRecurrentONNXWrapper 的参数列表。"
        )

    wrapper = E2VIDRecurrentONNXWrapper(model)

    # 使用 256x256 作为虚拟输入尺寸（可被 2^3=8 整除）。
    # H/W 为动态维度，导出后可适配任意传感器尺寸。
    dummy_h, dummy_w = 256, 256
    dummy_event = torch.zeros(1, num_bins, dummy_h, dummy_w)

    # 各编码层级的通道数: base_ch * 2^(i+1)
    # 空间尺寸: H / 2^(i+1)
    ch_levels = [base_ch * (2 ** (i + 1)) for i in range(num_encoders)]
    sp_levels = [dummy_h // (2 ** (i + 1)) for i in range(num_encoders)]

    dummy_states = []
    for ch, sp in zip(ch_levels, sp_levels):
        dummy_states.append(torch.zeros(1, ch, sp, sp))  # h
        dummy_states.append(torch.zeros(1, ch, sp, sp))  # c

    input_names = ["event_tensor", "h0", "c0", "h1", "c1", "h2", "c2"]
    output_names = ["image", "h0_new", "c0_new", "h1_new", "c1_new", "h2_new", "c2_new"]

    # 动态维度：batch 和空间维度可变，通道维度固定。
    # 每个层级使用独立的维度名，因为 H/2 != H。
    dynamic_axes = {
        "event_tensor": {0: "batch", 2: "H", 3: "W"},
        "image": {0: "batch", 2: "H", 3: "W"},
    }
    level_dim_names = [("H2", "W2"), ("H4", "W4"), ("H8", "W8")]
    for i in range(num_encoders):
        hn, cn = f"h{i}", f"c{i}"
        hn_new, cn_new = f"h{i}_new", f"c{i}_new"
        hd, wd = level_dim_names[i]
        dynamic_axes[hn] = {0: "batch", 2: hd, 3: wd}
        dynamic_axes[cn] = {0: "batch", 2: hd, 3: wd}
        dynamic_axes[hn_new] = {0: "batch", 2: hd, 3: wd}
        dynamic_axes[cn_new] = {0: "batch", 2: hd, 3: wd}

    print(f"Exporting to {output_path} ...")
    torch.onnx.export(
        wrapper,
        (dummy_event, *dummy_states),
        output_path,
        input_names=input_names,
        output_names=output_names,
        dynamic_axes=dynamic_axes,
        opset_version=opset,
        do_constant_folding=True,
        dynamo=False,  # 使用传统导出器（兼容性更好）
    )
    print("  Export complete.")


def export_non_recurrent(model, output_path, opset=17):
    """导出非循环 E2VID 模型（plain UNet，无状态）。"""
    wrapper = E2VIDONNXWrapper(model)
    num_bins = model.num_bins
    dummy_event = torch.zeros(1, num_bins, 256, 256)

    print(f"Exporting to {output_path} ...")
    torch.onnx.export(
        wrapper,
        dummy_event,
        output_path,
        input_names=["event_tensor"],
        output_names=["image"],
        dynamic_axes={
            "event_tensor": {0: "batch", 2: "H", 3: "W"},
            "image": {0: "batch", 2: "H", 3: "W"},
        },
        opset_version=opset,
        do_constant_folding=True,
        dynamo=False,  # 使用传统导出器（兼容性更好）
    )
    print("  Export complete.")


def verify(output_path):
    """验证导出的 ONNX 模型。"""
    print(f"Verifying {output_path} ...")
    onnx_model = onnx.load(output_path)
    onnx.checker.check_model(onnx_model)

    # 打印输入/输出信息。
    for i, inp in enumerate(onnx_model.graph.input):
        shape = [d.dim_value or d.dim_param or "?" for d in inp.type.tensor_type.shape.dim]
        print(f"  input[{i}]  {inp.name}: {shape}")
    for i, out in enumerate(onnx_model.graph.output):
        shape = [d.dim_value or d.dim_param or "?" for d in out.type.tensor_type.shape.dim]
        print(f"  output[{i}] {out.name}: {shape}")
    print("  ONNX model is valid.")


def main():
    parser = argparse.ArgumentParser(description="Convert rpg_e2vid .pth.tar to ONNX")
    parser.add_argument("--input", required=True, help="Path to .pth.tar model")
    parser.add_argument("--output", required=True, help="Path to output .onnx model")
    parser.add_argument(
        "--rpg-e2vid-dir",
        default=None,
        help="Path to ref/rpg_e2vid (default: auto-detect <repo>/ref/rpg_e2vid)",
    )
    parser.add_argument("--opset", type=int, default=17, help="ONNX opset version")
    args = parser.parse_args()

    # 自动检测 ref/rpg_e2vid 目录。
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    rpg_dir = args.rpg_e2vid_dir or os.path.join(repo_root, "ref", "rpg_e2vid")
    if not os.path.isdir(rpg_dir):
        print(f"Error: {rpg_dir} not found. Clone rpg_e2vid first or use --rpg-e2vid-dir.")
        sys.exit(1)

    model = load_model(args.input, rpg_dir)
    arch = model.__class__.__name__

    if arch == "E2VIDRecurrent":
        export_recurrent(model, args.output, opset=args.opset)
    elif arch == "E2VID":
        export_non_recurrent(model, args.output, opset=args.opset)
    else:
        print(f"Error: unsupported model arch '{arch}'")
        sys.exit(1)

    verify(args.output)


if __name__ == "__main__":
    main()
