#!/usr/bin/env python3
"""
Firmware updater with optional GUI for the ESP8266 `/update` endpoint.

Features:
- CLI upload: upload a local .bin to `http://<host>/update`
- GUI: two buttons — download latest firmware from a GitHub raw URL, and upload a chosen file

Usage (CLI):
  python3 tools/firmware_updater.py --host 192.168.4.1 --file firmware.bin

Usage (GUI):
  python3 tools/firmware_updater.py --gui

Requires: `requests` and standard `tkinter` for GUI.
"""

import argparse
import os
import sys
import threading
import tempfile

try:
    import requests
except ImportError:
    print("Missing dependency: requests. Install with: pip install requests")
    sys.exit(2)

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox
except Exception:
    tk = None


def upload(host, filepath, timeout=120, status_callback=None):
    url = f"http://{host}/update"
    if not os.path.isfile(filepath):
        msg = f"File not found: {filepath}"
        if status_callback:
            status_callback(msg)
        else:
            print(msg)
        return 2

    with open(filepath, 'rb') as f:
        files = {'update': (os.path.basename(filepath), f, 'application/octet-stream')}
        if status_callback:
            status_callback(f"Uploading {os.path.basename(filepath)} to {host}...")
        else:
            print(f"Uploading {filepath} -> {url}")
        try:
            r = requests.post(url, files=files, timeout=timeout)
        except requests.RequestException as e:
            msg = f"Request failed: {e}"
            if status_callback:
                status_callback(msg)
            else:
                print(msg)
            return 3

    msg = f"HTTP status: {r.status_code} - {r.text}"
    if status_callback:
        status_callback(msg)
    else:
        print(msg)
    return 0 if r.status_code in (200, 201) else 4


def download_file(url, dest_path, status_callback=None):
    try:
        with requests.get(url, stream=True, timeout=30) as r:
            r.raise_for_status()
            total = r.headers.get('content-length')
            if total is None:
                total = 0
            else:
                total = int(total)
            downloaded = 0
            with open(dest_path, 'wb') as f:
                for chunk in r.iter_content(chunk_size=8192):
                    if chunk:
                        f.write(chunk)
                        downloaded += len(chunk)
                        if status_callback:
                            status_callback(f"Downloaded {downloaded}/{total} bytes")
        if status_callback:
            status_callback("Download finished")
        return True
    except Exception as e:
        if status_callback:
            status_callback(f"Download failed: {e}")
        else:
            print("Download failed:", e)
        return False


def run_gui():
    if tk is None:
        print("Tkinter not available on this system")
        return

    root = tk.Tk()
    root.title('Firmware Updater SVM 2026')

    tk.Label(root, text='Cílové zařízení (IP)').grid(row=0, column=0, sticky='w')
    host_var = tk.StringVar(value='192.168.4.1')
    tk.Entry(root, textvariable=host_var, width=30).grid(row=0, column=1, columnspan=2)

    tk.Label(root, text='GitHub raw URL').grid(row=1, column=0, sticky='w')
    url_var = tk.StringVar(value='https://github.com/venucci-cze/casomira_firmware/releases/latest/download/firmware.bin')
    tk.Entry(root, textvariable=url_var, width=60).grid(row=1, column=1, columnspan=2)

    status_var = tk.StringVar(value='Připraveno')
    tk.Label(root, textvariable=status_var).grid(row=3, column=0, columnspan=3, sticky='w')

    temp_file = os.path.join(tempfile.gettempdir(), 'firmware_latest.bin')

    def set_status(text):
        status_var.set(text)
        root.update_idletasks()

    def do_download():
        url = url_var.get().strip()
        if not url:
            messagebox.showerror('Error', 'Please enter URL')
            return

        def worker():
            set_status('Downloading...')
            ok = download_file(url, temp_file, status_callback=set_status)
            if ok:
                messagebox.showinfo('Download', f'Downloaded to {temp_file}')
            else:
                messagebox.showerror('Download', 'Failed')

        threading.Thread(target=worker, daemon=True).start()

    def do_choose_and_upload():
        host = host_var.get().strip()
        if not host:
            messagebox.showerror('Error', 'Please enter device host')
            return
        filepath = filedialog.askopenfilename(title='Choose firmware .bin', filetypes=[('Binary','*.bin'),('All files','*.*')])
        if not filepath:
            return

        def worker():
            set_status('Uploading...')
            code = upload(host, filepath, status_callback=set_status)
            if code == 0:
                messagebox.showinfo('Upload', 'Upload finished')
            else:
                messagebox.showerror('Upload', f'Upload failed (code {code})')

        threading.Thread(target=worker, daemon=True).start()

    def do_upload_latest():
        host = host_var.get().strip()
        if not host:
            messagebox.showerror('Error', 'Please enter device host')
            return
        if not os.path.isfile(temp_file):
            messagebox.showerror('Error', 'No downloaded firmware found. Please Download first.')
            return

        def worker():
            set_status('Uploading latest...')
            code = upload(host, temp_file, status_callback=set_status)
            if code == 0:
                messagebox.showinfo('Upload', 'Upload finished')
            else:
                messagebox.showerror('Upload', f'Upload failed (code {code})')

        threading.Thread(target=worker, daemon=True).start()

    tk.Button(root, text='Stáhnout nejnovější', command=do_download).grid(row=2, column=1)
    tk.Button(root, text='Vybrat a nahrát', command=do_choose_and_upload).grid(row=2, column=2)
    tk.Button(root, text='Nahrání nejnovějšího', command=do_upload_latest).grid(row=2, column=0)

    root.mainloop()


def main_cli():
    p = argparse.ArgumentParser(description='Upload firmware to ESP8266 /update')
    p.add_argument('--host', required=True, help='IP or host of device (e.g. 192.168.4.1)')
    p.add_argument('--file', required=True, help='Path to firmware .bin file')
    p.add_argument('--timeout', type=int, default=120, help='HTTP timeout in seconds')
    args = p.parse_args()

    code = upload(args.host, args.file, timeout=args.timeout)
    sys.exit(code)


if __name__ == '__main__':
    # Default to GUI when no arguments are provided
    if len(sys.argv) == 1:
        run_gui()
        sys.exit(0)

    parser = argparse.ArgumentParser(description='Firmware updater (CLI + optional GUI)')
    parser.add_argument('--gui', action='store_true', help='Start GUI')
    parser.add_argument('--host', help='Device host for CLI mode')
    parser.add_argument('--file', help='Firmware file for CLI mode')
    args = parser.parse_args()

    if args.gui:
        run_gui()
    else:
        if not args.host or not args.file:
            print('For CLI mode provide --host and --file, or run with --gui for graphical mode')
            sys.exit(2)
        main_cli()
