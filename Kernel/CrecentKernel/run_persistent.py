import subprocess
import sys
import os
import binascii

def main():
    print("[HOST PERSIST] Starting run_persistent.py...")
    # 1. Build kernel if not built
    subprocess.run(["make"], shell=True)
    
    # 2. Run QEMU with serial redirected to stdio
    cmd = ["qemu-system-x86_64", "-kernel", "kernel.bin", "-initrd", "test.tar", "-serial", "stdio", "-device", "ac97"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    
    in_persist = False
    lines = []
    
    try:
        while True:
            line = proc.stdout.readline()
            if not line:
                break
            
            # Print to host terminal
            sys.stdout.write(line)
            sys.stdout.flush()
            
            stripped = line.strip()
            if stripped == "[PERSIST_BEGIN]":
                in_persist = True
                lines = []
            elif stripped == "[PERSIST_END]":
                in_persist = False
                process_persist(lines)
            elif in_persist:
                lines.append(stripped)
    except KeyboardInterrupt:
        print("[HOST PERSIST] Terminating QEMU...")
        proc.terminate()
        proc.wait()

def process_persist(lines):
    print("\n[HOST PERSIST] Received persist dump, processing...")
    cfg_lines = []
    
    for line in lines:
        if line.startswith("SCALE ") or line.startswith("THEME ") or line.startswith("WALLPAPER ") or line.startswith("ITEM "):
            cfg_lines.append(line)
        elif line.startswith("FILE "):
            parts = line.rsplit(" ", 2)
            if len(parts) >= 3:
                filename = parts[0][5:]
                size = int(parts[1])
                hex_data = parts[2]
                
                # Convert hex to binary data
                try:
                    data = binascii.unhexlify(hex_data[:size*2])
                except Exception as e:
                    print(f"[HOST PERSIST ERROR] Failed to decode hex for {filename}: {e}")
                    continue
                
                # Sanitize filename path under initrd
                clean_path = filename
                if clean_path.startswith("/"):
                    clean_path = clean_path[1:]
                if clean_path.startswith("tar/"):
                    clean_path = clean_path[4:]
                
                filepath = os.path.join("initrd", clean_path)
                os.makedirs(os.path.dirname(filepath), exist_ok=True)
                
                try:
                    with open(filepath, "wb") as f:
                        f.write(data)
                    print(f"[HOST PERSIST] Persisted file {filepath} ({size} bytes)")
                except Exception as e:
                    print(f"[HOST PERSIST ERROR] Failed to write file {filepath}: {e}")
    
    # Write desktop.cfg
    cfg_path = os.path.join("initrd", "desktop.cfg")
    try:
        with open(cfg_path, "w") as f:
            f.write("\n".join(cfg_lines) + "\n")
        print(f"[HOST PERSIST] Saved configuration to {cfg_path}")
    except Exception as e:
        print(f"[HOST PERSIST ERROR] Failed to write desktop.cfg: {e}")
        
    # Rebuild test.tar
    print("[HOST PERSIST] Rebuilding test.tar...")
    tar_cmd = ["tar", "-cf", "test.tar", "-C", "initrd", "."]
    res = subprocess.run(tar_cmd, shell=True)
    if res.returncode == 0:
        print("[HOST PERSIST] Rebuilt test.tar successfully.")
    else:
        print("[HOST PERSIST ERROR] Failed to rebuild test.tar.")

if __name__ == "__main__":
    main()
