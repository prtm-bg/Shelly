# Shelly: 
A custom shell for UNIX-based systems. 

# Usage

## Compilation

Shelly comes with a simple `Makefile`. To build the shell, simply run:
```bash
make
```

To clean up compiled binaries and object files:
```bash
make clean
```

## Running the Shell

After compiling, you can start the interactive shell session by running:
```bash
./mysh
```

## Features and Examples

Shelly supports many modern POSIX-like features via its custom Abstract Syntax Tree (AST) parser:

### 1. File Redirection
Redirect standard input and output seamlessly:
```bash
echo "Hello, World!" > output.txt
cat < output.txt
echo "Appending text" >> output.txt
```

### 2. Pipelining
Chain multiple processes together where the output of one process becomes the input to the next:
```bash
cat output.txt | grep "Hello" | wc -l
```

### 3. Background Jobs
Run processes asynchronously in the background using `&` so your shell doesn't freeze:
```bash
sleep 10 &
```
*(Shelly safely cleans up finished background processes under the hood using a `SIGCHLD` handler to prevent zombie processes).*

### 4. Logical Operators & Sequences
Execute commands conditionally or sequentially:
```bash
# AND: Runs only if the first command succeeds
make && ./mysh

# OR: Runs only if the first command fails
cat invalid_file.txt || echo "File not found!"

# SEQUENCE: Runs commands sequentially unconditionally
echo "Starting..."; sleep 2; echo "Done!"
```

### 5. Quoted Arguments
Shelly natively understands single and double quotes, allowing you to process paths and arguments with spaces without breaking the tokenizer:
```bash
mkdir "My New Folder"
cd 'My New Folder'
```

### 6. Built-in Commands
Shelly handles the following commands internally without forking external processes:
- `cd <path>`: Change directory.
- `pwd`: Print working directory.
- `clear`: Clear the terminal screen.
- `exit`: Safely terminate the shell.
