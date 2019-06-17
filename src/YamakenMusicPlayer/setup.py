import sys
import os
import time
import urllib.request
import zipfile
import shutil


def download_and_unzip(repo_name):
    user_name, project_name = repo_name.split('/')
    url = 'https://github.com/' + repo_name + '/archive/master.zip'
    urllib.request.urlretrieve(url, 'src/' + project_name + '.zip')
    if os.path.isdir('src/' + project_name):
        shutil.rmtree('src/' + project_name)
    while os.path.isdir('src/' + project_name):
        time.sleep(0.1)
    with zipfile.ZipFile('src/' + project_name + '.zip') as existing_zip:
        existing_zip.extractall('src/')
    shutil.move('src/' + project_name + '-master', 'src/' + project_name)
    os.remove('src/' + project_name + '.zip')

def rewrite(file, old, new):
    with open(file, 'r', encoding='utf8') as f:
        text = f.read()
    text = text.replace(old, new)
    with open(file, 'w', encoding='utf8') as f:
        f.write(text)

def main():
    download_and_unzip('kroimon/Arduino-SerialCommand')
    download_and_unzip('knolleary/pubsubclient')
    shutil.rmtree('src/pubsubclient/tests')
    shutil.rmtree('src/pubsubclient/examples')
    rewrite('src/pubsubclient/src/PubSubClient.h', 'MQTT_MAX_PACKET_SIZE 128', 'MQTT_MAX_PACKET_SIZE 256')
    rewrite('src/Arduino-SerialCommand/SerialCommand.h', 'SERIALCOMMAND_BUFFER 32', 'SERIALCOMMAND_BUFFER 64')

if __name__ == '__main__':
    main()
