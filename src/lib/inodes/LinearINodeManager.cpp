#include "LinearINodeManager.h"
#include "../FSExceptions.h"

#if defined(__linux__)
  #include <sys/statfs.h>
  #include <sys/statvfs.h>
  #include <sys/vfs.h>
#else
  #include <fuse.h>
#endif

LinearINodeManager::LinearINodeManager(Storage& storage): disk(&storage) {
  this->reload();
}

LinearINodeManager::~LinearINodeManager() {
  // Nothing to do.
}

void LinearINodeManager::mkfs() {
  this->reload();

  Block block;
  INode* inodes = (INode*) block.data;
  for(uint64_t i = 1; i < block_count*32*8/330; ++i) {
    for (uint64_t inode_index = 0; inode_index < (Block::SIZE / INode::SIZE); inode_index++) {
      inodes[inode_index].type = FileType::RESERVED;
    }
    this->disk->set(start_block + i, block);
  }
  
  std::memset(block.data, 0, Block::SIZE);
  for(uint64_t i = block_count*32*8/330; i < block_count; ++i) {
    this->disk->set(start_block + i, block);
  }

  // Reserve INodes for null and root:
  INode* inodes = (INode*) block.data;
  inodes[0].type = FileType::RESERVED;
  inodes[1].type = FileType::RESERVED;
  this->disk->set(start_block, block);
}

// Get an inode from the freelist and return it
INode::ID LinearINodeManager::reserve() {
  Block block;
  uint64_t num_inodes_per_block = Block::SIZE / INode::SIZE;
  for(uint64_t i = 0; i < block_count; i++) {
    this->disk->get(start_block + i, block);
    INode* inodes = (INode*) block.data;

    for (uint64_t j = 0; j < num_inodes_per_block; j++) {
      if(inodes[j].type == FileType::FREE) {
        return i * num_inodes_per_block + j;
      }
    }
  }

  throw OutOfINodes();
}

// Free an inode and return to the freelist
void LinearINodeManager::release(INode::ID inode_num) {
  if (inode_num >= this->num_inodes || inode_num < this->root) {
    throw std::out_of_range("INode index is out of range!");
  }

  uint64_t num_inodes_per_block = (Block::SIZE / INode::SIZE);
  uint64_t block_index = inode_num / num_inodes_per_block;
  uint64_t inode_index = inode_num % num_inodes_per_block;

  // Load the inode and modify attribute
  Block block;
  this->disk->get(start_block + block_index, block);
  INode *inode = (INode *) &(block.data[inode_index * INode::SIZE]);
  inode->type = FileType::FREE;

  // Write the inode back to disk
  this->disk->set(start_block + block_index, block);
}

void LinearINodeManager::reload() {
  Block block;
  Superblock* superblock = (Superblock*) &block;
  this->disk->get(0, block);

  assert(Block::SIZE % INode::SIZE == 0);
  uint64_t num_inodes_per_block = Block::SIZE / INode::SIZE;
  start_block = superblock->inode_block_start;
  block_count = superblock->inode_block_count;
  num_inodes  = num_inodes_per_block * block_count;
}

// Reads an inode from disk into the memory provided by the user
void LinearINodeManager::get(INode::ID inode_num, INode& user_inode) {
  if (inode_num >= this->num_inodes || inode_num < this->root) {
    throw std::out_of_range("INode index is out of range!");
  }

  uint64_t num_inodes_per_block = (Block::SIZE / INode::SIZE);
  uint64_t block_index = inode_num / num_inodes_per_block;
  uint64_t inode_index = inode_num % num_inodes_per_block;

  Block block;
  this->disk->get(start_block + block_index, block);
  INode *inode = (INode *) &(block.data[inode_index * INode::SIZE]);

  memcpy(&user_inode, inode, INode::SIZE);
}

void LinearINodeManager::set(INode::ID inode_num, const INode& user_inode) {
  if (inode_num >= this->num_inodes || inode_num < this->root) {
    throw std::out_of_range("INode index is out of range!");
  }

  uint64_t num_inodes_per_block = (Block::SIZE / INode::SIZE);
  uint64_t block_index = inode_num / num_inodes_per_block;
  uint64_t inode_index = inode_num % num_inodes_per_block;

  Block block;
  this->disk->get(start_block + block_index, block);
  INode *inode = (INode *) &(block.data[inode_index * INode::SIZE]);

  memcpy(inode, &user_inode, INode::SIZE);
  this->disk->set(start_block + block_index, block);
}

INode::ID LinearINodeManager::getRoot() {
  return this->root;
}

void LinearINodeManager::statfs(struct statvfs* info) {
  // Based on http://pubs.opengroup.org/onlinepubs/009604599/basedefs/sys/statvfs.h.html
  // Also see http://man7.org/linux/man-pages/man3/statvfs.3.html
  info->f_files  = num_inodes; // Total number of file serial numbers.
  info->f_ffree  = 42; //TODO! // Total number of free file serial numbers.
  info->f_favail = 42; //TODO! // Number of file serial numbers available to non-privileged process.
}
