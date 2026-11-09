import subprocess
def run(d, mv):
    i=f"uci\nsetoption name Depth value {d}\nisready\nposition startpos moves {' '.join(mv)}\ngo movetime 300\nquit\n"
    o=subprocess.run(["./ren_uci"],input=i,capture_output=True,text=True).stdout
    return [l for l in o.splitlines() if l.startswith("bestmove")][0].split()[1]
mv=[];diff=0
for _ in range(20):
    a=run(1,mv);b=run(8,mv);diff+=(a!=b);mv.append(b)
print("beda di",diff,"dari 20")
