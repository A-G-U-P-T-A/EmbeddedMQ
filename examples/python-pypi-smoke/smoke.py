from embeddedmq import Runtime

rt = Runtime()
q = rt.create_queue("orders", 64)
q.push(b"hello-pypi")
msg = q.pop()
assert msg.data() == b"hello-pypi", msg.data()
print("python ok:", msg.data())
