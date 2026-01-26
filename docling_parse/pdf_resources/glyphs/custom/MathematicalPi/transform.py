data_uni = []
data_hex = []

with open("MathematicalPi.dat.orig", "r") as fd:

    lines = fd.readlines()

    for line in lines:
        parts = line.replace("\n", "").split(";")

        if parts[1].startswith("0001"):
            val = parts[1].replace("0001", "1")

        try:
            dec = int(parts[1].replace(" ", ""), 16)
            data_uni.append([parts[0], chr(dec), val])

        except:
            data_hex.append([parts[0], parts[1]])

with open("MathematicalPi.hex.dat", "w") as fd:
    for row in data_hex:
        fd.write(";".join(row) + "\n")

with open("MathematicalPi.uni.dat", "w") as fd:
    for row in data_uni:
        fd.write(";".join(row) + "\n")
