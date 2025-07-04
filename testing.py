import socket

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.connect(("192.168.1.100", 23))
readFile = server.makefile('r')

while (True):
    message = input("Command to send: ")
    print(f">> {message}")
    server.send(f"{message}\r\n".encode())
    if (message.endswith("?")):
        print(f"<< {readFile.readline()}")