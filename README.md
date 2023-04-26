# asteriskify - Linux console password prompt with asterisks

## Usage
```
asteriskify | cryptsetup luksOpen ...
```

Pipe it to anything that receives credentials over stdin, such as cryptsetup.

To switch between clear and asterisk mode, press TAB. To reveal only the last character, use CTRL + R.

Note: Expects ASCII or UTF-8 encoding for asterisk mode

## Alternatives
Others may exist, but my quick research only found systemd-ask-password which is unsuitable for my use case (inside a my own minimal initramfs boot image).

