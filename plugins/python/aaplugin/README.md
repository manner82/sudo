# Description

This directory contains an example on how the sudo Python Plugin API can be useful.
It implements a wrapper between the approval plugins of sudo and the also opensource
Safeguard Sessions AAPlugins.

It contains:
 - a sudo python approval plugin 'aa-plugin-wrapper.py' which is able to use an AAPlugin
   and implements the bridge between the two plugin types
 - and a shell script 'sudo-config-aa.sh' which helps installing a Safeguard Sessions AAPlugin
   for use with sudo.

If you build and install sudo from this branch, these get installed on the system.


# How to use an AAPlugin?

#### Step 1: Get the release

Download the AAPlugin release, see a list of possible plugins [here](https://github.com/search?q=topic:oi-sps-plugin+org:OneIdentity+fork:true). Note that only AAPlugins are supported by this and not "CredentialStore" plugins.

You can either just download the latest release, or build them with issuing the following commands inside the AAPlugin's source directory:
```
# setup the python env for the plugin
# (You might need to install some dependencies for this to work, eg. on debian:
#  apt-get -y install libsasl2-dev python3-dev libldap2-dev libssl-dev libffi-dev pipenv)
pipenv install -d              

# step into the env
SHELL=/bin/bash pipenv shell
# build a zip from the plugin
pluginv release
```

#### Step 2: Use it for sudo authentication

Install the AAPlugin on the system:
```
# This will extract the zip under "sudo python plugin directory"/aa/
# and creates an initial configuration under "/etc/sudo.aa.d/"
# (also helps with possible problems):
sudo-config-aa.sh the-plugin.zip      
```
Follow the guidance of the script:

- Edit the configuration of the AAPlugin under `/etc/sudo.aa.d/NameOfThePlugin.conf`

- Register the plugin into `/etc/sudo.conf` by adding the line:
```
Plugin python_approval python_plugin.so ModulePath=aa_plugin_wrapper.py AAPlugin=NameOfThePlugin
```

# Some examples

## YubiKey

(Beware: I am extreme noob in this)

0. Check that YubiKey can generate OTP (One Time Password):

    Give input focus to some text field (eg. the terminal).
    Press slightly the button on the YubiKey: it should type a lot of characters into the terminal.

    If not, open yubikey-personalization-gui's OTP tab and upload to slot1 the magics I have no idea about (I used the defaults).

1. Download the release from [here](https://github.com/OneIdentity/safeguard-sessions-plugin-yubikey-mfa/releases/latest).

    Install the plugin:
    
    ```
    sudo-config-aa.sh /mnt/home/manner/ttemp/SPS_Yubikey-2.0.1.zip
    ```

2. Get Yubico Client ID and API key by registering the device [here](https://upgrade.yubico.com/getapikey/).

    Get your YubiKey Public ID (you'll need to map this to the actual user running sudo):
    generate an OTP (see step 0) and your public id is the first 12 characters.

3. Configure them for the plugin

```
$ cat /etc/sudo.aa.d/SPS_Yubikey.conf
[yubikey]
client_id=69196
api_key=uUCJpy6NbTUF2J2gO3ybMU/LxiU=

[usermapping source=explicit]
testuser=vvvvjglmbiid
```

4. Register the AAPlugin to sudo
  ```
  echo "Plugin python_approval python_plugin.so ModulePath=aa_plugin_wrapper.py AAPlugin=SPS_Yubikey" >>/etc/sudo.conf
  ```

5. Try
  (You need to press the ubikey when sudo is asking for the one-time password.)
  
  ```
  $ sudo id
  Press Enter for push notification or type one-time password:
  uid=0(root) gid=0(root) groups=0(root)
  ```

## Starling

0. Create a starling organization (eg. DEMO account) with a 2FA service.
   Download the Starling 2FA app for your phone and set it up.

1. Download the plugin from [here](https://github.com/OneIdentity/safeguard-sessions-plugin-starling-2fa/releases/latest).

    Install the plugin:
    ```
	sudo-config-aa.sh SPS_Starling-2.2.2.zip
    ```

    (Currently starling credential string can not be set from the config.
    It is implemented on my branch: https://github.com/manner82/safeguard-sessions-plugin-starling-2fa/tree/credential_string_config_override
    So you'll need to build from that for now.)

2. Get a credential string from [here](https://account-test.cloud.oneidentity.com/join/Safeguard/sudo).
   Get the starling user id from the Starling 2FA application in your phone.

3. Configure them for the plugin:
```
$ cat /etc/sudo.aa.d/SPS_Starling.conf
[starling]
; if your using the test environment:
environment=test
; replace this with your credential string:
credential_string=d4e9dcd9-d8e9-4d61-9593-278924d18eef:532ba0c4-b144-44a2-caeb-087ab5b3fd3c

[usermapping source=explicit]
; replace this with the user who will invoke sudo, and its starling user id:
testuser=125821324
```

4. Register the AAPlugin to sudo:
```
echo "Plugin python_approval python_plugin.so ModulePath=aa_plugin_wrapper.py AAPlugin=SPS_Starling" >>/etc/sudo.conf
```

5. Try
(You either need to press enter to "accept on your phone", or type the password the phone generates.)
```
$ sudo id
Press Enter for push notification or type one-time password:
uid=0(root) gid=0(root) groups=0(root)
```
