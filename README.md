# SEC-SMACK-CP

This project provides 2 programs:

- an extractor, smack-extract, that extract the smack labels
  and produce a file recording them

- a restorer, smack-restore, that, using the file recording
  smack labels, restores it.

## Extracting extended attributes

The program `sec-xattr-extract`:

```
sec-xattr-extract [-d] [-m pattern] OUT-FILE ROOT-DIR
```

Extract in `OUT-FILE` the extended attributes of files at `ROOT-DIR`
directory.

By default extract any extended attribute. But if an extended regular expression
is given in `pattern` using option `-m`, only these patterns are extracted.

The option `-d`dumps out the extracted attributes.

## Restoring extended attributes

The program `sec-xattr-restore`:

```
sec-xattr-rectore [-d] IN-FILE ROOT-DIR [program [arg ...]]
```

Set the extended attributes extracted in `IN-FILE` to files at `ROOT-DIR`.

The option '-d' is a dump out dry run of the process.

When program is given, on success, the restorer executes it,
calling it with its optional arguments.


## Format of the file recording the labels

The file contains 3 sections: ID CODE STRINGS

### Section ID

The ID section is 16 bytes long (alignment on 4 bytes boundary
must be preserved)

For the version 1 the ID is the string "sec-xattr-cp 1\n\n"

```
0000000   s   e   c   -   x   a   t   t   r   -   c   p       1  \n  \n
         73  65  63  2d  78  61  74  74  72  2d  63  70  20  31  0a  0a
```

### Section CODE

The section code is made of aligned 32 bits unsigned integers, named code,
recorded in little endian order.

Each of these code is made of 2 lower bits recording the operation
and 30 upper bits being an offset to a data argument.

```
  3 3 2
  1 0 9  ..  ...      2 1 0
 +-+-+-+ . . . . . . +-+-+-+
 | o f f s e t         | op|
 +-+-+-+ . . . . . . +-+-+-+
```

Operations are SUB (0), FILE (1), ATTR (2), SET(3).

The data argument of the operation is the data located at the given offset,
computed by adding 4 + OFFSET to the adress of the code of the operation.

The below C fragment shows the process of retrieving the operation OP
and the pointer to the DATA for a code pointed by PCODE


```C
		CODE = *PCODE++;
		CODE = le32toh(CODE);
		DATA = &((char*)PCODE)[CODE >> 2];
        OP = CODE & 3;
```

The data is of to types: STRINGZ or BUFFER.

STRINGZ are zero terminated strings UTF8 encoded. It is in use
for operation SUB, FILE and ATTR. That string can be retrived using the
below C code:

```C
        STR = DATA;
```


BUFFER is a byte array whose length is encoded in 16 bits little endian
at the start of the data. It is in use for SET operation.
Be aware that the 16-bits integer may be not aligned on the 2 bytes boundary.
The LENGTH and the CONTENT can be retrieved using the below C code:

```C
        LENGTH = DATA[0] + 256 * DATA[1];
        CONTENT = &DATA[2];
```


#### Operation SUB

This is for entering or leaving a directory.

This operation has 2 cases. When the offset is 0 it means to pop
the last entered directory or to terminate the process.

Otherwise, the string records the directory to enter.

#### Operation FILE

Set the file to be changed.

#### Operation ATTR

Set the attribute to be changed.

#### Operation SET

Set the current file with the current attribute with the value
described at the offset. The value is recorded with its length
given in 2 bytes little endian followed by the value without
trailing zero.

### Section STRING

Strings are zero terminated.

