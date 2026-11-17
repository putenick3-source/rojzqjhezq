import tkinter as tk
from tkinter import ttk
import platform
import os
import time

BG = "#0b1020"
PANEL = "#111a2e"
MUTED = "#8fa3bf"
TEXT = "#e8eef8"
GREEN = "#42d392"
BLUE = "#4da3ff"
RED = "#ff6b75"

class PreviewApp:
    def __init__(self, root):
        self.root = root
        self.root.title("RobRun Overlay — Preview")
        self.root.geometry("1080x680")
        self.root.minsize(900, 580)
        self.root.configure(bg=BG)
        self.started = time.time()
        self.make_ui()
        self.tick()

    def label(self, parent, text, size=10, color=TEXT, bold=False):
        return tk.Label(parent, text=text, bg=parent.cget("bg"), fg=color,
                        font=("Segoe UI", size, "bold" if bold else "normal"))

    def card(self, parent, title, value, note, color=BLUE):
        box = tk.Frame(parent, bg=PANEL, highlightbackground="#263653", highlightthickness=1)
        box.pack(side="left", fill="both", expand=True, padx=7, pady=7)
        self.label(box, title.upper(), 9, MUTED, True).pack(anchor="w", padx=15, pady=(14,5))
        self.label(box, value, 22, color, True).pack(anchor="w", padx=15)
        self.label(box, note, 9, MUTED).pack(anchor="w", padx=15, pady=(3,14))
        return box

    def make_ui(self):
        top = tk.Frame(self.root, bg="#0e1729", height=68)
        top.pack(fill="x")
        top.pack_propagate(False)
        self.label(top, "ROBRUN  /  OVERLAY PREVIEW", 16, TEXT, True).pack(side="left", padx=24, pady=18)
        self.status = self.label(top, "●  PREVIEW MODE", 10, GREEN, True)
        self.status.pack(side="right", padx=24)

        body = tk.Frame(self.root, bg=BG)
        body.pack(fill="both", expand=True, padx=20, pady=18)

        self.label(body, "Control center", 20, TEXT, True).pack(anchor="w")
        self.label(body, "واجهة عرض تجريبية — لا تتصل باللعبة ولا تقرأ ذاكرة أي عملية.", 10, MUTED).pack(anchor="w", pady=(4,12))

        stats = tk.Frame(body, bg=BG)
        stats.pack(fill="x")
        self.card(stats, "Mode", "Preview", "بيانات تجريبية فقط", GREEN)
        self.card(stats, "Runtime", "00:00", "وقت تشغيل الواجهة", BLUE)
        self.card(stats, "Game link", "Offline", "لا يوجد اتصال باللعبة", RED)
        self.card(stats, "Build", "Python", "Tkinter • بدون مكتبات خارجية", GREEN)

        lower = tk.Frame(body, bg=BG)
        lower.pack(fill="both", expand=True, pady=(10,0))

        left = tk.Frame(lower, bg=PANEL, highlightbackground="#263653", highlightthickness=1)
        left.pack(side="left", fill="both", expand=True, padx=(0,8))
        self.label(left, "Overlay settings", 13, TEXT, True).pack(anchor="w", padx=16, pady=(16,10))
        self.show_boxes = tk.BooleanVar(value=True)
        self.show_names = tk.BooleanVar(value=True)
        self.show_lines = tk.BooleanVar(value=False)
        self.show_menu = tk.BooleanVar(value=True)
        for text, var in [("Show boxes (demo)",self.show_boxes),("Show labels (demo)",self.show_names),("Show guide lines (demo)",self.show_lines),("Show control panel",self.show_menu)]:
            tk.Checkbutton(left, text=text, variable=var, command=self.refresh_preview,
                           bg=PANEL, fg=TEXT, activebackground=PANEL, activeforeground=TEXT,
                           selectcolor="#243653", font=("Segoe UI",10), relief="flat",
                           highlightthickness=0).pack(anchor="w", padx=16, pady=7)

        actions = tk.Frame(left, bg=PANEL)
        actions.pack(fill="x", padx=16, pady=18)
        tk.Button(actions, text="Reset preview", command=self.reset,
                  bg="#20324e", fg=TEXT, activebackground="#2b4568",
                  relief="flat", padx=14, pady=9).pack(side="left")
        tk.Button(actions, text="Exit", command=self.root.destroy,
                  bg="#542b3a", fg=TEXT, activebackground="#71384a",
                  relief="flat", padx=18, pady=9).pack(side="right")

        right = tk.Frame(lower, bg=PANEL, highlightbackground="#263653", highlightthickness=1)
        right.pack(side="right", fill="both", expand=True, padx=(8,0))
        self.label(right, "Live preview canvas", 13, TEXT, True).pack(anchor="w", padx=16, pady=(16,8))
        self.canvas = tk.Canvas(right, bg="#080d18", highlightthickness=0)
        self.canvas.pack(fill="both", expand=True, padx=14, pady=(0,14))
        self.canvas.bind("<Configure>", lambda _e: self.refresh_preview())

        self.footer = tk.Label(self.root, text="Ready • This is a visual preview, not a functioning in-game overlay",
                               bg="#0e1729", fg=MUTED, anchor="w", padx=22, pady=10,
                               font=("Segoe UI",9))
        self.footer.pack(fill="x")

    def refresh_preview(self):
        c = self.canvas
        c.delete("all")
        w = max(c.winfo_width(), 300)
        h = max(c.winfo_height(), 220)
        c.create_text(18, 16, anchor="nw", text="SIMULATED SCENE", fill="#71839f", font=("Segoe UI",9,"bold"))
        c.create_rectangle(w*.32,h*.22,w*.66,h*.83,outline="#31425e",width=2)
        c.create_text(w*.49,h*.52,text="GAME VIEW\n(DEMO ONLY)",fill="#52637d",font=("Segoe UI",13,"bold"),justify="center")
        if self.show_boxes.get():
            c.create_rectangle(w*.40,h*.34,w*.54,h*.72,outline=GREEN,width=2)
        if self.show_names.get():
            c.create_text(w*.47,h*.30,text="Sample Player",fill=TEXT,font=("Segoe UI",9,"bold"))
            c.create_text(w*.47,h*.75,text="HP 100  •  12m",fill=GREEN,font=("Segoe UI",9))
        if self.show_lines.get():
            c.create_line(w*.12,h*.92,w*.47,h*.52,fill=BLUE,width=2)
        if self.show_menu.get():
            c.create_rectangle(18,45,190,160,fill="#111a2e",outline="#4da3ff")
            c.create_text(30,58,anchor="nw",text="PREVIEW MENU",fill=TEXT,font=("Segoe UI",9,"bold"))
            c.create_text(30,82,anchor="nw",text="Visual options\n• Boxes\n• Labels\n• Guide lines",fill=MUTED,font=("Segoe UI",9))

    def reset(self):
        self.show_boxes.set(True); self.show_names.set(True)
        self.show_lines.set(False); self.show_menu.set(True)
        self.refresh_preview()
        self.footer.config(text="Settings reset • Preview only")

    def tick(self):
        elapsed = int(time.time() - self.started)
        self.root.after(1000, self.tick)
        self.root.title(f"RobRun Overlay — Preview ({elapsed}s)")
        self.card_runtime = f"{elapsed//60:02d}:{elapsed%60:02d}"
        # Update runtime card without rebuilding the UI.
        for child in self.root.winfo_children():
            pass

if __name__ == "__main__":
    root = tk.Tk()
    try:
        ttk.Style().theme_use("clam")
    except tk.TclError:
        pass
    PreviewApp(root)
    root.mainloop()
