# CS3650-Filesystem
Simple FUSE filesystem based on CS3650 challenge assignment

## Description
This is a simplified EXT-like filesystem that supports nested directories, symbolic links, and primitive bookeeping information. This filesystem makes the assumption that the only the owning user is mounting the filesystem. To emulate a block device a file of 256MiB is generated and `mmap`ed into userspace, allowing for changes to persist across several mounts and unmounts.

## How to Run

It's fairly simple to run the filesystem. Open a new terminal, go to the project root directory and use the `mount` target

```
$ make mount
```

Now, a new `mnt/` directory should appear which is the mounted FUSE filesystem.

## Testing

This has not been thouroughly tested, but some hand crafted testing has been accomplished. I have tried the following with success:
- Placing many files in the same folder
- Nesting many files within many folders
- Making several sym-links to the same folder far away in the directory tree and modifing it.
- Creating a simple C source file (like a hello-world program), compiling it, and running it
- Cloning a small repository using git
  - Making changes
  - Making a commit
  - Showing logs correctly
  - Branching
  - Building the Git project
