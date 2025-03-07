#include "epoll.h"
#include "main.h"

int nepoll_create(int size)
{
  int fd = get_fd_from_bitmap();
  struct eventpoll *ep = rte_malloc("eventpoll ", sizeof(struct eventpoll), 0);
  ep->fd = fd;
  ep->rbcnt = 0;
  RB_INIT(&ep->rbr);
  LIST_INIT(&ep->rdlist);

  pthread_mutex_init(&ep->mtx, NULL);
  pthread_mutex_init(&ep->cdmtx, NULL);
  pthread_cond_init(&ep->cond, NULL);
  pthread_spin_init(&ep->lock, PTHREAD_PROCESS_SHARED);

  struct tcp_streams *tcp_list_inst = tcp_list_instance();
  tcp_list_inst->ep = ep;

  return fd;
}

int nepoll_ctl(int epfd, int op, int sockid, struct epoll_event *event)
{
  struct eventpoll *ep = find_host_by_fd(epfd);
  if (!ep || (!event && op != EPOLL_CTL_DEL))
  {
    rte_errno = -EINVAL;
    return -1;
  }

  if (op == EPOLL_CTL_ADD)
  {

    pthread_mutex_lock(&ep->mtx);

    struct epitem tmp;
    tmp.sockfd = sockid;
    struct epitem *epi = RB_FIND(_epoll_rb_socket, &ep->rbr, &tmp);
    if (epi)
    {
      pthread_mutex_unlock(&ep->mtx);
      return -1;
    }

    epi = (struct epitem *)rte_malloc("epitem", sizeof(struct epitem), 0);
    if (!epi)
    {
      pthread_mutex_unlock(&ep->mtx);
      rte_errno = -ENOMEM;
      return -1;
    }

    epi->sockfd = sockid;
    memcpy(&epi->event, event, sizeof(struct epoll_event));

    epi = RB_INSERT(_epoll_rb_socket, &ep->rbr, epi);
    assert(epi == NULL);
    ep->rbcnt++;

    pthread_mutex_unlock(&ep->mtx);
  }
  else if (op == EPOLL_CTL_DEL)
  {

    pthread_mutex_lock(&ep->mtx);

    struct epitem tmp;
    tmp.sockfd = sockid;
    struct epitem *epi = RB_FIND(_epoll_rb_socket, &ep->rbr, &tmp);
    if (!epi)
    {
      pthread_mutex_unlock(&ep->mtx);
      return -1;
    }

    epi = RB_REMOVE(_epoll_rb_socket, &ep->rbr, epi);
    if (!epi)
    {
      pthread_mutex_unlock(&ep->mtx);
      return -1;
    }

    ep->rbcnt--;
    rte_free(epi);

    pthread_mutex_unlock(&ep->mtx);
  }
  else if (op == EPOLL_CTL_MOD)
  {

    struct epitem tmp;
    tmp.sockfd = sockid;
    struct epitem *epi = RB_FIND(_epoll_rb_socket, &ep->rbr, &tmp);
    if (epi)
    {
      epi->event.events = event->events;
      epi->event.events |= EPOLLERR | EPOLLHUP;
    }
    else
    {
      rte_errno = -ENOENT;
      return -1;
    }
  }

  return 0;
}
int nepoll_wait(int epfd, struct epoll_event *events, int maxevents,
                int timeout)
{

  struct eventpoll *ep = find_host_by_fd(epfd);
  if (!ep || !events || maxevents <= 0)
  {
    rte_errno = -EINVAL;
    return -1;
  }

  if (pthread_mutex_lock(&ep->cdmtx))
  {
    if (rte_errno == EDEADLK)
    {
      printf("epoll lock blocked\n");
    }
    assert(0);
  }

  while (ep->rdnum == 0 && timeout != 0)
  {

    ep->waiting = 1;
    if (timeout > 0)
    {

      struct timespec deadline;

      clock_gettime(CLOCK_REALTIME, &deadline);
      if (timeout >= 1000)
      {
        int sec;
        sec = timeout / 1000;
        deadline.tv_sec += sec;
        timeout -= sec * 1000;
      }

      deadline.tv_nsec += timeout * 1000000;

      if (deadline.tv_nsec >= 1000000000)
      {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
      }

      int ret = pthread_cond_timedwait(&ep->cond, &ep->cdmtx, &deadline);
      if (ret && ret != ETIMEDOUT)
      {
        pthread_mutex_unlock(&ep->cdmtx);

        return -1;
      }
      timeout = 0;
    }
    else if (timeout < 0)
    {

      int ret = pthread_cond_wait(&ep->cond, &ep->cdmtx);
      if (ret)
      {
        pthread_mutex_unlock(&ep->cdmtx);

        return -1;
      }
    }
    ep->waiting = 0;
  }

  pthread_mutex_unlock(&ep->cdmtx);

  pthread_spin_lock(&ep->lock);

  int cnt = 0;
  int num = (ep->rdnum > maxevents ? maxevents : ep->rdnum);
  int i = 0;

  while (num != 0 && !LIST_EMPTY(&ep->rdlist))
  { // EPOLLET

    struct epitem *epi = LIST_FIRST(&ep->rdlist);
    LIST_REMOVE(epi, rdlink);
    epi->rdy = 0;

    memcpy(&events[i++], &epi->event, sizeof(struct epoll_event));

    num--;
    cnt++;
    ep->rdnum--;
  }

  pthread_spin_unlock(&ep->lock);

  return cnt;
}

int epoll_event_callback(struct eventpoll *ep, int sockid, uint32_t event)
{

  struct epitem tmp;
  tmp.sockfd = sockid;
  struct epitem *epi = RB_FIND(_epoll_rb_socket, &ep->rbr, &tmp);
  if (!epi)
  {
    assert(0);
  }
  // this event has been processed
  if (epi->rdy)
  {
    epi->event.events |= event;
    return 1;
  }

  pthread_spin_lock(&ep->lock);
  epi->rdy = 1;
  LIST_INSERT_HEAD(&ep->rdlist, epi, rdlink);
  ep->rdnum++;
  pthread_spin_unlock(&ep->lock);

  pthread_mutex_lock(&ep->cdmtx);

  pthread_cond_signal(&ep->cond);
  pthread_mutex_unlock(&ep->cdmtx);
  return 0;
}
