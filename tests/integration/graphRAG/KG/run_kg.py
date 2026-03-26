import socket
import time

HOST = "127.0.0.1"
PORT = 7777

def receive(sock):
    """Receive and print server response"""
    try:
        data = sock.recv(4096).decode()
        if data:
            print(f"[SERVER]\n{data}", end="")
        return data
    except socket.timeout:
        return ""

def send(sock, message):
    """Send and print client message"""
    print(f"[CLIENT] {message}")
    sock.sendall((message + "\n").encode())

def send_and_log(sock, message, delay=0.5):
    receive(sock)
    send(sock, message)
    time.sleep(delay)

def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        s.settimeout(2)

        # Initial server message
        receive(s)

        steps = [
            "constructkg",
            "n",
            "10.8.100.248",
            "9000",
            "/home/hotpot_subset_500MB.txt",
            "http://10.8.100.21:6578:32,http://10.8.100.22:6580:32,http://10.8.100.26:6578:32",
            "vllm",
            "google/gemma-3-4b-it",
            "1024",
            "n"
        ]

        for step in steps:
            send_and_log(s, step)

        # Final response
        time.sleep(1)
        receive(s)

        print("\n[Done]")

if __name__ == "__main__":
    main()