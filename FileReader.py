import sendText
from pathlib2 import Path
import time
import os

f = open('config.txt', 'w')

f.write("+17202357772\n")
f.write("destgps:(50,50)\n")
f.close()

initialise = True

while (True):
    my_file = Path("/home/linaro/Documents/TextMessage")
    if my_file.is_file():
        content = my_file.read_text()
        os.remove(str(my_file))

        print("sending: ", content)
        sendText.text(str(content))

    time.sleep(10)
