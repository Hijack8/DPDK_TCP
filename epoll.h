#ifndef __EPOLL_H_
#define __EPOLL_H_

#include "main.h"

enum EPOLL_EVENTS {
  EPOLLIN = 0x001,
#define EPOLLIN EPOLLIN
  EPOLLPRI = 0x002,
#define EPOLLPRI EPOLLPRI
  EPOLLOUT = 0x004,
#define EPOLLOUT EPOLLOUT
  EPOLLRDNORM = 0x040,
#define EPOLLRDNORM EPOLLRDNORM
  EPOLLRDBAND = 0x080,
#define EPOLLRDBAND EPOLLRDBAND
  EPOLLWRNORM = 0x100,
#define EPOLLWRNORM EPOLLWRNORM
  EPOLLWRBAND = 0x200,
#define EPOLLWRBAND EPOLLWRBAND
  EPOLLMSG = 0x400,
#define EPOLLMSG EPOLLMSG
  EPOLLERR = 0x008,
#define EPOLLERR EPOLLERR
  EPOLLHUP = 0x010,
#define EPOLLHUP EPOLLHUP
  EPOLLRDHUP = 0x2000,
#define EPOLLRDHUP EPOLLRDHUP
  EPOLLONESHOT = (1 << 30),
#define EPOLLONESHOT EPOLLONESHOT
  EPOLLET = (1 << 31)
#define EPOLLET EPOLLET
};

/* Valid opcodes ( "op" parameter ) to issue to epoll_ctl().  */
#define EPOLL_CTL_ADD 1 /* Add a file decriptor to the interface.  */
#define EPOLL_CTL_DEL 2 /* Remove a file decriptor from the interface.  */
#define EPOLL_CTL_MOD 3 /* Change file decriptor epoll_event structure.  */

typedef union epoll_data {
  void *ptr;
  int fd;
  __uint32_t u32;
  __uint64_t u64;
} epoll_data_t;

struct epoll_event {
  __uint32_t events; /* epoll event */
  epoll_data_t data; /* User data variable */
};

int nepoll_create(int size);
int nepoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int nepoll_wait(int epfd, struct epoll_event *events, int maxevents,
                int timeout);

/* Macros that define a red-black tree */
#define RB_HEAD(name, type)                                                    \
  struct name {                                                                \
    struct type *rbh_root; /* root of the tree */                              \
  }

#define RB_INITIALIZER(root) {NULL}

#define RB_INIT(root)                                                          \
  do {                                                                         \
    (root)->rbh_root = NULL;                                                   \
  } while (0)

#define RB_BLACK 0
#define RB_RED 1

#define RB_ENTRY(type)                                                         \
  struct {                                                                     \
    struct type *rbe_left;   /* left element */                                \
    struct type *rbe_right;  /* right element */                               \
    struct type *rbe_parent; /* parent element */                              \
    int rbe_color;           /* node color */                                  \
  }

#define RB_LEFT(elm, field) (elm)->field.rbe_left
#define RB_RIGHT(elm, field) (elm)->field.rbe_right
#define RB_PARENT(elm, field) (elm)->field.rbe_parent
#define RB_COLOR(elm, field) (elm)->field.rbe_color
#define RB_ROOT(head) (head)->rbh_root
#define RB_EMPTY(head) (RB_ROOT(head) == NULL)

#define RB_SET(elm, parent, field)                                             \
  do {                                                                         \
    RB_PARENT(elm, field) = parent;                                            \
    RB_LEFT(elm, field) = RB_RIGHT(elm, field) = NULL;                         \
    RB_COLOR(elm, field) = RB_RED;                                             \
  } while (0)

#define RB_SET_BLACKRED(black, red, field)                                     \
  do {                                                                         \
    RB_COLOR(black, field) = RB_BLACK;                                         \
    RB_COLOR(red, field) = RB_RED;                                             \
  } while (0)

#ifndef RB_AUGMENT
#define RB_AUGMENT(x)                                                          \
  do {                                                                         \
  } while (0)
#endif

#define RB_ROTATE_LEFT(head, elm, tmp, field)                                  \
  do {                                                                         \
    (tmp) = RB_RIGHT(elm, field);                                              \
    if ((RB_RIGHT(elm, field) = RB_LEFT(tmp, field)) != NULL) {                \
      RB_PARENT(RB_LEFT(tmp, field), field) = (elm);                           \
    }                                                                          \
    RB_AUGMENT(elm);                                                           \
    if ((RB_PARENT(tmp, field) = RB_PARENT(elm, field)) != NULL) {             \
      if ((elm) == RB_LEFT(RB_PARENT(elm, field), field))                      \
        RB_LEFT(RB_PARENT(elm, field), field) = (tmp);                         \
      else                                                                     \
        RB_RIGHT(RB_PARENT(elm, field), field) = (tmp);                        \
    } else                                                                     \
      (head)->rbh_root = (tmp);                                                \
    RB_LEFT(tmp, field) = (elm);                                               \
    RB_PARENT(elm, field) = (tmp);                                             \
    RB_AUGMENT(tmp);                                                           \
    if ((RB_PARENT(tmp, field)))                                               \
      RB_AUGMENT(RB_PARENT(tmp, field));                                       \
  } while (0)

#define RB_ROTATE_RIGHT(head, elm, tmp, field)                                 \
  do {                                                                         \
    (tmp) = RB_LEFT(elm, field);                                               \
    if ((RB_LEFT(elm, field) = RB_RIGHT(tmp, field)) != NULL) {                \
      RB_PARENT(RB_RIGHT(tmp, field), field) = (elm);                          \
    }                                                                          \
    RB_AUGMENT(elm);                                                           \
    if ((RB_PARENT(tmp, field) = RB_PARENT(elm, field)) != NULL) {             \
      if ((elm) == RB_LEFT(RB_PARENT(elm, field), field))                      \
        RB_LEFT(RB_PARENT(elm, field), field) = (tmp);                         \
      else                                                                     \
        RB_RIGHT(RB_PARENT(elm, field), field) = (tmp);                        \
    } else                                                                     \
      (head)->rbh_root = (tmp);                                                \
    RB_RIGHT(tmp, field) = (elm);                                              \
    RB_PARENT(elm, field) = (tmp);                                             \
    RB_AUGMENT(tmp);                                                           \
    if ((RB_PARENT(tmp, field)))                                               \
      RB_AUGMENT(RB_PARENT(tmp, field));                                       \
  } while (0)

#define RB_PROTOTYPE(name, type, field, cmp)                                   \
  RB_PROTOTYPE_INTERNAL(name, type, field, cmp, )
#define RB_PROTOTYPE_STATIC(name, type, field, cmp)                            \
  RB_PROTOTYPE_INTERNAL(name, type, field, cmp, __unused static)
#define RB_PROTOTYPE_INTERNAL(name, type, field, cmp, attr)                    \
  attr void name##_RB_INSERT_COLOR(struct name *, struct type *);              \
  attr void name##_RB_REMOVE_COLOR(struct name *, struct type *,               \
                                   struct type *);                             \
  attr struct type *name##_RB_REMOVE(struct name *, struct type *);            \
  attr struct type *name##_RB_INSERT(struct name *, struct type *);            \
  attr struct type *name##_RB_FIND(struct name *, struct type *);              \
  attr struct type *name##_RB_NFIND(struct name *, struct type *);             \
  attr struct type *name##_RB_NEXT(struct type *);                             \
  attr struct type *name##_RB_PREV(struct type *);                             \
  attr struct type *name##_RB_MINMAX(struct name *, int);

/* Main rb operation.
 * Moves node close to the key of elm to top
 */
#define RB_GENERATE(name, type, field, cmp)                                    \
  RB_GENERATE_INTERNAL(name, type, field, cmp, )
#define RB_GENERATE_STATIC(name, type, field, cmp)                             \
  RB_GENERATE_INTERNAL(name, type, field, cmp, static)
#define RB_GENERATE_INTERNAL(name, type, field, cmp, attr)                     \
  attr void name##_RB_INSERT_COLOR(struct name *head, struct type *elm) {      \
    struct type *parent, *gparent, *tmp;                                       \
    while ((parent = RB_PARENT(elm, field)) != NULL &&                         \
           RB_COLOR(parent, field) == RB_RED) {                                \
      gparent = RB_PARENT(parent, field);                                      \
      if (parent == RB_LEFT(gparent, field)) {                                 \
        tmp = RB_RIGHT(gparent, field);                                        \
        if (tmp && RB_COLOR(tmp, field) == RB_RED) {                           \
          RB_COLOR(tmp, field) = RB_BLACK;                                     \
          RB_SET_BLACKRED(parent, gparent, field);                             \
          elm = gparent;                                                       \
          continue;                                                            \
        }                                                                      \
        if (RB_RIGHT(parent, field) == elm) {                                  \
          RB_ROTATE_LEFT(head, parent, tmp, field);                            \
          tmp = parent;                                                        \
          parent = elm;                                                        \
          elm = tmp;                                                           \
        }                                                                      \
        RB_SET_BLACKRED(parent, gparent, field);                               \
        RB_ROTATE_RIGHT(head, gparent, tmp, field);                            \
      } else {                                                                 \
        tmp = RB_LEFT(gparent, field);                                         \
        if (tmp && RB_COLOR(tmp, field) == RB_RED) {                           \
          RB_COLOR(tmp, field) = RB_BLACK;                                     \
          RB_SET_BLACKRED(parent, gparent, field);                             \
          elm = gparent;                                                       \
          continue;                                                            \
        }                                                                      \
        if (RB_LEFT(parent, field) == elm) {                                   \
          RB_ROTATE_RIGHT(head, parent, tmp, field);                           \
          tmp = parent;                                                        \
          parent = elm;                                                        \
          elm = tmp;                                                           \
        }                                                                      \
        RB_SET_BLACKRED(parent, gparent, field);                               \
        RB_ROTATE_LEFT(head, gparent, tmp, field);                             \
      }                                                                        \
    }                                                                          \
    RB_COLOR(head->rbh_root, field) = RB_BLACK;                                \
  }                                                                            \
                                                                               \
  attr void name##_RB_REMOVE_COLOR(struct name *head, struct type *parent,     \
                                   struct type *elm) {                         \
    struct type *tmp;                                                          \
    while ((elm == NULL || RB_COLOR(elm, field) == RB_BLACK) &&                \
           elm != RB_ROOT(head)) {                                             \
      if (RB_LEFT(parent, field) == elm) {                                     \
        tmp = RB_RIGHT(parent, field);                                         \
        if (RB_COLOR(tmp, field) == RB_RED) {                                  \
          RB_SET_BLACKRED(tmp, parent, field);                                 \
          RB_ROTATE_LEFT(head, parent, tmp, field);                            \
          tmp = RB_RIGHT(parent, field);                                       \
        }                                                                      \
        if ((RB_LEFT(tmp, field) == NULL ||                                    \
             RB_COLOR(RB_LEFT(tmp, field), field) == RB_BLACK) &&              \
            (RB_RIGHT(tmp, field) == NULL ||                                   \
             RB_COLOR(RB_RIGHT(tmp, field), field) == RB_BLACK)) {             \
          RB_COLOR(tmp, field) = RB_RED;                                       \
          elm = parent;                                                        \
          parent = RB_PARENT(elm, field);                                      \
        } else {                                                               \
          if (RB_RIGHT(tmp, field) == NULL ||                                  \
              RB_COLOR(RB_RIGHT(tmp, field), field) == RB_BLACK) {             \
            struct type *oleft;                                                \
            if ((oleft = RB_LEFT(tmp, field)) != NULL)                         \
              RB_COLOR(oleft, field) = RB_BLACK;                               \
            RB_COLOR(tmp, field) = RB_RED;                                     \
            RB_ROTATE_RIGHT(head, tmp, oleft, field);                          \
            tmp = RB_RIGHT(parent, field);                                     \
          }                                                                    \
          RB_COLOR(tmp, field) = RB_COLOR(parent, field);                      \
          RB_COLOR(parent, field) = RB_BLACK;                                  \
          if (RB_RIGHT(tmp, field))                                            \
            RB_COLOR(RB_RIGHT(tmp, field), field) = RB_BLACK;                  \
          RB_ROTATE_LEFT(head, parent, tmp, field);                            \
          elm = RB_ROOT(head);                                                 \
          break;                                                               \
        }                                                                      \
      } else {                                                                 \
        tmp = RB_LEFT(parent, field);                                          \
        if (RB_COLOR(tmp, field) == RB_RED) {                                  \
          RB_SET_BLACKRED(tmp, parent, field);                                 \
          RB_ROTATE_RIGHT(head, parent, tmp, field);                           \
          tmp = RB_LEFT(parent, field);                                        \
        }                                                                      \
        if ((RB_LEFT(tmp, field) == NULL ||                                    \
             RB_COLOR(RB_LEFT(tmp, field), field) == RB_BLACK) &&              \
            (RB_RIGHT(tmp, field) == NULL ||                                   \
             RB_COLOR(RB_RIGHT(tmp, field), field) == RB_BLACK)) {             \
          RB_COLOR(tmp, field) = RB_RED;                                       \
          elm = parent;                                                        \
          parent = RB_PARENT(elm, field);                                      \
        } else {                                                               \
          if (RB_LEFT(tmp, field) == NULL ||                                   \
              RB_COLOR(RB_LEFT(tmp, field), field) == RB_BLACK) {              \
            struct type *oright;                                               \
            if ((oright = RB_RIGHT(tmp, field)) != NULL)                       \
              RB_COLOR(oright, field) = RB_BLACK;                              \
            RB_COLOR(tmp, field) = RB_RED;                                     \
            RB_ROTATE_LEFT(head, tmp, oright, field);                          \
            tmp = RB_LEFT(parent, field);                                      \
          }                                                                    \
          RB_COLOR(tmp, field) = RB_COLOR(parent, field);                      \
          RB_COLOR(parent, field) = RB_BLACK;                                  \
          if (RB_LEFT(tmp, field))                                             \
            RB_COLOR(RB_LEFT(tmp, field), field) = RB_BLACK;                   \
          RB_ROTATE_RIGHT(head, parent, tmp, field);                           \
          elm = RB_ROOT(head);                                                 \
          break;                                                               \
        }                                                                      \
      }                                                                        \
    }                                                                          \
    if (elm)                                                                   \
      RB_COLOR(elm, field) = RB_BLACK;                                         \
  }                                                                            \
                                                                               \
  attr struct type *name##_RB_REMOVE(struct name *head, struct type *elm) {    \
    struct type *child, *parent, *old = elm;                                   \
    int color;                                                                 \
    if (RB_LEFT(elm, field) == NULL)                                           \
      child = RB_RIGHT(elm, field);                                            \
    else if (RB_RIGHT(elm, field) == NULL)                                     \
      child = RB_LEFT(elm, field);                                             \
    else {                                                                     \
      struct type *left;                                                       \
      elm = RB_RIGHT(elm, field);                                              \
      while ((left = RB_LEFT(elm, field)) != NULL)                             \
        elm = left;                                                            \
      child = RB_RIGHT(elm, field);                                            \
      parent = RB_PARENT(elm, field);                                          \
      color = RB_COLOR(elm, field);                                            \
      if (child)                                                               \
        RB_PARENT(child, field) = parent;                                      \
      if (parent) {                                                            \
        if (RB_LEFT(parent, field) == elm)                                     \
          RB_LEFT(parent, field) = child;                                      \
        else                                                                   \
          RB_RIGHT(parent, field) = child;                                     \
        RB_AUGMENT(parent);                                                    \
      } else                                                                   \
        RB_ROOT(head) = child;                                                 \
      if (RB_PARENT(elm, field) == old)                                        \
        parent = elm;                                                          \
      (elm)->field = (old)->field;                                             \
      if (RB_PARENT(old, field)) {                                             \
        if (RB_LEFT(RB_PARENT(old, field), field) == old)                      \
          RB_LEFT(RB_PARENT(old, field), field) = elm;                         \
        else                                                                   \
          RB_RIGHT(RB_PARENT(old, field), field) = elm;                        \
        RB_AUGMENT(RB_PARENT(old, field));                                     \
      } else                                                                   \
        RB_ROOT(head) = elm;                                                   \
      RB_PARENT(RB_LEFT(old, field), field) = elm;                             \
      if (RB_RIGHT(old, field))                                                \
        RB_PARENT(RB_RIGHT(old, field), field) = elm;                          \
      if (parent) {                                                            \
        left = parent;                                                         \
        do {                                                                   \
          RB_AUGMENT(left);                                                    \
        } while ((left = RB_PARENT(left, field)) != NULL);                     \
      }                                                                        \
      goto color;                                                              \
    }                                                                          \
    parent = RB_PARENT(elm, field);                                            \
    color = RB_COLOR(elm, field);                                              \
    if (child)                                                                 \
      RB_PARENT(child, field) = parent;                                        \
    if (parent) {                                                              \
      if (RB_LEFT(parent, field) == elm)                                       \
        RB_LEFT(parent, field) = child;                                        \
      else                                                                     \
        RB_RIGHT(parent, field) = child;                                       \
      RB_AUGMENT(parent);                                                      \
    } else                                                                     \
      RB_ROOT(head) = child;                                                   \
  color:                                                                       \
    if (color == RB_BLACK)                                                     \
      name##_RB_REMOVE_COLOR(head, parent, child);                             \
    return (old);                                                              \
  }                                                                            \
                                                                               \
  /* Inserts a node into the RB tree */                                        \
  attr struct type *name##_RB_INSERT(struct name *head, struct type *elm) {    \
    struct type *tmp;                                                          \
    struct type *parent = NULL;                                                \
    int comp = 0;                                                              \
    tmp = RB_ROOT(head);                                                       \
    while (tmp) {                                                              \
      parent = tmp;                                                            \
      comp = (cmp)(elm, parent);                                               \
      if (comp < 0)                                                            \
        tmp = RB_LEFT(tmp, field);                                             \
      else if (comp > 0)                                                       \
        tmp = RB_RIGHT(tmp, field);                                            \
      else                                                                     \
        return (tmp);                                                          \
    }                                                                          \
    RB_SET(elm, parent, field);                                                \
    if (parent != NULL) {                                                      \
      if (comp < 0)                                                            \
        RB_LEFT(parent, field) = elm;                                          \
      else                                                                     \
        RB_RIGHT(parent, field) = elm;                                         \
      RB_AUGMENT(parent);                                                      \
    } else                                                                     \
      RB_ROOT(head) = elm;                                                     \
    name##_RB_INSERT_COLOR(head, elm);                                         \
    return (NULL);                                                             \
  }                                                                            \
                                                                               \
  /* Finds the node with the same key as elm */                                \
  attr struct type *name##_RB_FIND(struct name *head, struct type *elm) {      \
    struct type *tmp = RB_ROOT(head);                                          \
    int comp;                                                                  \
    while (tmp) {                                                              \
      comp = cmp(elm, tmp);                                                    \
      if (comp < 0)                                                            \
        tmp = RB_LEFT(tmp, field);                                             \
      else if (comp > 0)                                                       \
        tmp = RB_RIGHT(tmp, field);                                            \
      else                                                                     \
        return (tmp);                                                          \
    }                                                                          \
    return (NULL);                                                             \
  }                                                                            \
                                                                               \
  /* Finds the first node greater than or equal to the search key */           \
  attr struct type *name##_RB_NFIND(struct name *head, struct type *elm) {     \
    struct type *tmp = RB_ROOT(head);                                          \
    struct type *res = NULL;                                                   \
    int comp;                                                                  \
    while (tmp) {                                                              \
      comp = cmp(elm, tmp);                                                    \
      if (comp < 0) {                                                          \
        res = tmp;                                                             \
        tmp = RB_LEFT(tmp, field);                                             \
      } else if (comp > 0)                                                     \
        tmp = RB_RIGHT(tmp, field);                                            \
      else                                                                     \
        return (tmp);                                                          \
    }                                                                          \
    return (res);                                                              \
  }                                                                            \
                                                                               \
  /* ARGSUSED */                                                               \
  attr struct type *name##_RB_NEXT(struct type *elm) {                         \
    if (RB_RIGHT(elm, field)) {                                                \
      elm = RB_RIGHT(elm, field);                                              \
      while (RB_LEFT(elm, field))                                              \
        elm = RB_LEFT(elm, field);                                             \
    } else {                                                                   \
      if (RB_PARENT(elm, field) &&                                             \
          (elm == RB_LEFT(RB_PARENT(elm, field), field)))                      \
        elm = RB_PARENT(elm, field);                                           \
      else {                                                                   \
        while (RB_PARENT(elm, field) &&                                        \
               (elm == RB_RIGHT(RB_PARENT(elm, field), field)))                \
          elm = RB_PARENT(elm, field);                                         \
        elm = RB_PARENT(elm, field);                                           \
      }                                                                        \
    }                                                                          \
    return (elm);                                                              \
  }                                                                            \
                                                                               \
  /* ARGSUSED */                                                               \
  attr struct type *name##_RB_PREV(struct type *elm) {                         \
    if (RB_LEFT(elm, field)) {                                                 \
      elm = RB_LEFT(elm, field);                                               \
      while (RB_RIGHT(elm, field))                                             \
        elm = RB_RIGHT(elm, field);                                            \
    } else {                                                                   \
      if (RB_PARENT(elm, field) &&                                             \
          (elm == RB_RIGHT(RB_PARENT(elm, field), field)))                     \
        elm = RB_PARENT(elm, field);                                           \
      else {                                                                   \
        while (RB_PARENT(elm, field) &&                                        \
               (elm == RB_LEFT(RB_PARENT(elm, field), field)))                 \
          elm = RB_PARENT(elm, field);                                         \
        elm = RB_PARENT(elm, field);                                           \
      }                                                                        \
    }                                                                          \
    return (elm);                                                              \
  }                                                                            \
                                                                               \
  attr struct type *name##_RB_MINMAX(struct name *head, int val) {             \
    struct type *tmp = RB_ROOT(head);                                          \
    struct type *parent = NULL;                                                \
    while (tmp) {                                                              \
      parent = tmp;                                                            \
      if (val < 0)                                                             \
        tmp = RB_LEFT(tmp, field);                                             \
      else                                                                     \
        tmp = RB_RIGHT(tmp, field);                                            \
    }                                                                          \
    return (parent);                                                           \
  }

#define RB_NEGINF -1
#define RB_INF 1

#define RB_INSERT(name, x, y) name##_RB_INSERT(x, y)
#define RB_REMOVE(name, x, y) name##_RB_REMOVE(x, y)
#define RB_FIND(name, x, y) name##_RB_FIND(x, y)
#define RB_NFIND(name, x, y) name##_RB_NFIND(x, y)
#define RB_NEXT(name, x, y) name##_RB_NEXT(y)
#define RB_PREV(name, x, y) name##_RB_PREV(y)
#define RB_MIN(name, x) name##_RB_MINMAX(x, RB_NEGINF)
#define RB_MAX(name, x) name##_RB_MINMAX(x, RB_INF)

#define RB_FOREACH(x, name, head)                                              \
  for ((x) = RB_MIN(name, head); (x) != NULL; (x) = name##_RB_NEXT(x))

#define RB_FOREACH_FROM(x, name, y)                                            \
  for ((x) = (y); ((x) != NULL) && ((y) = name##_RB_NEXT(x), (x) != NULL);     \
       (x) = (y))

#define RB_FOREACH_SAFE(x, name, head, y)                                      \
  for ((x) = RB_MIN(name, head);                                               \
       ((x) != NULL) && ((y) = name##_RB_NEXT(x), (x) != NULL); (x) = (y))

#define RB_FOREACH_REVERSE(x, name, head)                                      \
  for ((x) = RB_MAX(name, head); (x) != NULL; (x) = name##_RB_PREV(x))

#define RB_FOREACH_REVERSE_FROM(x, name, y)                                    \
  for ((x) = (y); ((x) != NULL) && ((y) = name##_RB_PREV(x), (x) != NULL);     \
       (x) = (y))

#define RB_FOREACH_REVERSE_SAFE(x, name, head, y)                              \
  for ((x) = RB_MAX(name, head);                                               \
       ((x) != NULL) && ((y) = name##_RB_PREV(x), (x) != NULL); (x) = (y))

struct epitem {
  RB_ENTRY(epitem) rbn;
  LIST_ENTRY(epitem) rdlink;
  int rdy; // exist in list

  int sockfd;
  struct epoll_event event;
};

static int sockfd_cmp(struct epitem *ep1, struct epitem *ep2) {
  if (ep1->sockfd < ep2->sockfd)
    return -1;
  else if (ep1->sockfd == ep2->sockfd)
    return 0;
  return 1;
}

RB_HEAD(_epoll_rb_socket, epitem);
RB_GENERATE_STATIC(_epoll_rb_socket, epitem, rbn, sockfd_cmp);

typedef struct _epoll_rb_socket ep_rb_tree;

struct eventpoll {
  int fd;
  ep_rb_tree rbr;
  int rbcnt;

  LIST_HEAD(, epitem) rdlist;
  int rdnum;

  int waiting;

  pthread_mutex_t mtx;     // rbtree update
  pthread_spinlock_t lock; // rdlist update

  pthread_cond_t cond;   // block for event
  pthread_mutex_t cdmtx; // mutex for cond
};

int epoll_event_callback(struct eventpoll *ep, int sockid, uint32_t event);

#endif
