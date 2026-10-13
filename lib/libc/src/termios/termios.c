#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

int tcgetattr(int fd, struct termios *termios) {
    return ioctl(fd, TCGETS, termios);
}

int tcsetattr(int fd, int action, const struct termios *termios) {
    static const unsigned long requests[] = { TCSETS, TCSETSW, TCSETSF };
    if (action < TCSANOW || action > TCSAFLUSH) {
        errno = EINVAL;
        return -1;
    }
    return ioctl(fd, requests[action], termios);
}

int tcflush(int fd, int queue) {
    return ioctl(fd, TCFLSH, (void *)(unsigned long)queue);
}

int tcdrain(int fd) {
    struct termios termios;
    return tcgetattr(fd, &termios);
}

pid_t tcgetpgrp(int fd) {
    pid_t pgrp;
    return ioctl(fd, TIOCGPGRP, &pgrp) < 0 ? -1 : pgrp;
}

int tcsetpgrp(int fd, pid_t pgrp) {
    return ioctl(fd, TIOCSPGRP, &pgrp);
}

pid_t tcgetsid(int fd) {
    pid_t session;
    return ioctl(fd, TIOCGSID, &session) < 0 ? -1 : session;
}

void cfmakeraw(struct termios *termios) {
    termios->c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    termios->c_oflag &= ~(tcflag_t)OPOST;
    termios->c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    termios->c_cflag &= ~(tcflag_t)(CSIZE | PARENB);
    termios->c_cflag |= CS8;
    termios->c_cc[VMIN] = 1;
    termios->c_cc[VTIME] = 0;
}

speed_t cfgetispeed(const struct termios *termios) {
    return termios->__c_ispeed;
}

speed_t cfgetospeed(const struct termios *termios) {
    return termios->__c_ospeed;
}

int cfsetispeed(struct termios *termios, speed_t speed) {
    termios->__c_ispeed = speed;
    return 0;
}

int cfsetospeed(struct termios *termios, speed_t speed) {
    termios->__c_ospeed = speed;
    return 0;
}
