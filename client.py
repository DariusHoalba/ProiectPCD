import socket
import os
import struct

BUFFER_SIZE = 4096
PORT = 12345
END_SIGNAL = "done"
END_SIGNAL_LEN = len(END_SIGNAL)

username = ""
password = ""

def admin_menu(client_socket):
    print("Admin logged in.")

    while True:
        print("Admin Options:")
        print("1. See users")
        print("2. See active users")
        print("3. Add users")
        print("4. Delete users")
        print("5. Stats")
        print("6. Shutdown server")
        print("7. Uptime")
        print("Type the number of the desired operation or 'done' to exit:")

        command = input().strip()
        if command == 'done':
            client_socket.close()
            print("Disconnected from server.")
            exit(0)

        try:
            command = int(command)
            if 1 <= command <= 7:
                net_command = struct.pack('!I', command)
                client_socket.sendall(net_command)

                if command == 1:
                    response = client_socket.recv(BUFFER_SIZE).decode()
                    print("List of users:\n", response)
                elif command == 2:
                    response = client_socket.recv(BUFFER_SIZE).decode()
                    print("List of active users:\n", response)
                elif command == 3:
                    new_username = input("Enter new username: ").strip()
                    client_socket.sendall(new_username.encode())
                    new_password = input("Enter new password: ").strip()
                    client_socket.sendall(new_password.encode())
                    response = client_socket.recv(BUFFER_SIZE).decode()
                    print(response)
                elif command == 4:
                    del_username = input("Enter username to delete: ").strip()
                    client_socket.sendall(del_username.encode())
                    response = client_socket.recv(BUFFER_SIZE).decode()
                    print(response)
                elif command == 5:
                    response = client_socket.recv(BUFFER_SIZE).decode()
                    print("Stats:\n", response)
                elif command == 6:
                    print("Server is shutting down. You will be disconnected")
                    client_socket.close()
                    exit(0)
                elif command == 7:
                    response = client_socket.recv(BUFFER_SIZE).decode()
                    print("Uptime: ", response)
            else:
                print("Invalid operation code. Please enter a valid operation code (1-7) or 'done' to finish.")
        except ValueError:
            print("Invalid input. Please enter a number.")

def generate_modified_filename(input_filename):
    dot_position = input_filename.rfind('.')
    if dot_position != -1:
        return f"{input_filename[:dot_position]}_modified{input_filename[dot_position:]}"
    else:
        return f"{input_filename}_modified"

def is_valid_extension(filename):
    valid_extensions = [".jpeg", ".jpg", ".bmp", ".png"]
    dot = filename.rfind('.')
    if dot == -1:
        return False
    return filename[dot:].lower() in valid_extensions

def send_file(client_socket, filepath):
    with open(filepath, "rb") as file:
        buffer = file.read(BUFFER_SIZE)
        while buffer:
            client_socket.sendall(buffer)
            buffer = file.read(BUFFER_SIZE)
    client_socket.sendall(END_SIGNAL.encode())
    print(f"File {filepath} sent successfully.")

def process_files_in_directory(client_socket, directory_path, operation_code):
    try:
        for entry in os.listdir(directory_path):
            filepath = os.path.join(directory_path, entry)
            if not os.path.isfile(filepath) or not is_valid_extension(filepath):
                continue

            send_file(client_socket, filepath)

            confirmation = client_socket.recv(BUFFER_SIZE).decode()
            if not confirmation:
                print("Failed to receive confirmation from server")
                break
            print(f"Server confirmation: {confirmation}")

            client_socket.sendall(struct.pack('!I', operation_code))

            output_filename = generate_modified_filename(filepath)
            with open(output_filename, "wb") as output_file:
                print(f"Receiving processed data for {filepath}...")
                while True:
                    buffer = client_socket.recv(BUFFER_SIZE)
                    if buffer.endswith(END_SIGNAL.encode()):
                        output_file.write(buffer[:-END_SIGNAL_LEN])
                        break
                    output_file.write(buffer)
            print(f"Processed file received and saved as {output_filename}.")
    except Exception as e:
        print(f"Error processing files in directory: {e}")

def main():
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_address = ('127.0.0.1', PORT)

    try:
        client_socket.connect(server_address)
    except socket.error as e:
        print(f"Connection to server failed: {e}")
        client_socket.close()
        return

    while True:
        option = ''
        while option not in ['1', '2']:
            option = input("Enter 1 for login, 2 for registration: ").strip()

        client_socket.sendall(option.encode())

        global username
        global password

        username = input("Enter username: ").strip()
        client_socket.sendall(username.encode())

        password = input("Enter password: ").strip()
        client_socket.sendall(password.encode())

        response = client_socket.recv(BUFFER_SIZE).decode()
        #print(response)
        if option == '1':
            if "Invalid username or password" in response:
                print("Invalid username or password. Please try again.")
                continue
            elif "User already connected" in response:
                print("User already connected. Please try again.")
                continue
            else:
                if "You are an admin." in response:
                    admin_menu(client_socket)
                else:
                    break
        elif option == '2':
            if "Registration failed" in response:
                continue
            else:
                print("Registration successful. Please log in.")
                continue

    print("Connected to server. Enter directory path to send files (Type 'done' to finish):")

    while True:
        directory_path = input("Enter directory path: ").strip()

        if directory_path.lower() == 'done':
            client_socket.sendall(END_SIGNAL.encode())
            break

        if not os.path.isdir(directory_path):
            print("Specified path is not a directory, please enter a valid directory path.")
            continue

        while True:
            print("Select operation to perform on all files in the directory:")
            print("1. Invert colors")
            print("2. Rotate 90 degrees")
            print("3. Rotate 180 degrees")
            print("4. Rotate 270 degrees")
            print("5. Convert to black/white")
            input_buffer = input("Enter operation code (or 'done' to finish): ").strip()

            if input_buffer.lower() == 'done':
                break

            try:
                operation_code = int(input_buffer)
                if 1 <= operation_code <= 5:
                    break
                else:
                    print("Invalid operation code. Please enter a valid operation code (1-5) or 'done' to finish.")
            except ValueError:
                print("Invalid input. Please enter a valid number.")

        if input_buffer.lower() == 'done':
            break

        process_files_in_directory(client_socket, directory_path, operation_code)

    client_socket.close()
    print("Disconnected from server.")

if __name__ == "__main__":
    main()
