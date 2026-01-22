import time
import requests

# 如果你在 worker1 上跑，就用 127.0.0.1；在其它机器上就写 worker1 的 IP
URL = "http://127.0.0.1:30000/v1/chat/completions"
MODEL = "qwen/qwen2.5-0.5b-instruct"

# 一个长一点的 prompt：让模型写一篇多段落的技术说明文
LONG_PROMPT = """
你现在扮演一名资深大模型工程师，请用结构化的方式详细回答下面的问题。

问题：解释在两台服务器上使用 Tensor Parallel（张量并行）部署大语言模型做推理时的关键点，包括但不限于：
1. 为什么需要 Tensor Parallel，以及它和 Data Parallel、Pipeline Parallel 的区别。
2. 在两台服务器上做 TP=2 时，模型权重是如何被切分和放置的。
3. 在一次完整的推理过程中，请按“时间顺序”描述一次请求从进入 HTTP 接口，到在两台 GPU 上协同计算，再到返回结果的整个流程。
4. 多机场景下常见的通信问题（例如 NCCL 初始化失败、连接被对端关闭）有哪些表现？应该如何排查？
5. 请给出一份“给运维同事看的简明检查清单”，帮助他们确认多机 TP 服务是否健康运行（包括需要看哪些日志、检查哪些命令等）。

要求：
- 使用分级标题（例如：一、二、三… 或 1. 2. 3.）。
- 每一部分要写成 2〜3 段完整文字，而不是只有要点列表。
- 回答不少于 200 字。
"""

def send_one_request(i: int):
    payload = {
        "model": MODEL,
        "messages": [
            {"role": "user", "content": LONG_PROMPT}
        ],
        # 生成多一点，延长推理时间
        "max_tokens": 800,
        "temperature": 0.7,
        "top_p": 0.9,
    }

    start = time.time()
    resp = requests.post(
        URL,
        json=payload,
        # 强制本次请求不走环境代理，避免代理捣乱
        proxies={"http": None, "https": None},
        timeout=300,
    )
    elapsed = time.time() - start

    print(f"\n=== 请求 {i} 结束，用时 {elapsed:.2f} 秒，HTTP {resp.status_code} ===")
    if resp.ok:
        data = resp.json()
        content = data["choices"][0]["message"]["content"]
        print(content[:800])  # 只打印前 800 字符，避免刷屏
    else:
        print("失败响应：", resp.text[:500])


def main():
    # 连续发 5 次请求，方便你一边跑一边看 nvidia-smi
    for i in range(1, 3):
        send_one_request(i)
        # 两次之间稍微停顿一下，避免日志太挤
        time.sleep(2)


if __name__ == "__main__":
    main()
