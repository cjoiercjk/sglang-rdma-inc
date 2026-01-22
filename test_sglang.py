import requests

url = "http://localhost:30000/v1/chat/completions"
payload = {
    "model": "qwen/qwen2.5-0.5b-instruct",
    "messages": [
        {"role": "user", "content": "用两句话解释一下你是做什么用的。"}
    ],
}

resp = requests.post(
    url,
    json=payload,
    timeout=30,
    # 保险起见：这行可以强制当前请求不使用任何代理
    proxies={"http": None, "https": None},
)

print("status:", resp.status_code)
print("raw text:", resp.text[:500])

if resp.ok:
    data = resp.json()
    print("\n模型回答：", data["choices"][0]["message"]["content"])
else:
    print("\n请求失败，HTTP 状态码：", resp.status_code)
