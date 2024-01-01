import socket

def main():
    host = '127.0.0.1'  # Server IP address
    port = 1234         # Port used by your server

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))

        while True:
            word = input("Enter a word (or 'q' to exit): ")
            if word.lower() == 'q':
                break

            s.sendall(word.encode())

            # Receiving data from server
            data = s.recv(32768)
            print("Received from server:", data.decode())

if __name__ == "__main__":
    main()

