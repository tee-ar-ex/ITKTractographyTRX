Test inputs live in this directory.

For binary or large files, store a matching checksum alongside the file as
`<filename>.sha512`. Example:

  cmake -E sha512sum mydata.trx > mydata.trx.sha512
