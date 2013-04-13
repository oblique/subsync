package main

import (
	"fmt"
	"errors"
	"strings"
	"strconv"
	"math"
	"os"
	"path"
	"io"
	"bufio"
	"container/list"
	"github.com/jessevdk/go-flags"
)

type subtitle struct {
	text string
	start uint
	end uint
}

func die(err error) {
	fmt.Fprintf(os.Stderr, "%v\n", err)
	os.Exit(1)
}

func roundFloat64(f float64) float64 {
	val := f - float64(int64(f))
	if val >= 0.5 {
		return math.Ceil(f)
	} else if val > 0 {
		return math.Floor(f)
	} else if val <= -0.5 {
		return math.Floor(f)
	} else if val < 0 {
		return math.Ceil(f)
	}
	return f
}

/* converts hh:mm:ss,mss to milliseconds */
func time_to_msecs(tm string) (uint, error) {
	var msecs uint
	var h, m, s, ms uint

	tm = strings.Replace(tm, ".", ",", 1)
	num, err := fmt.Sscanf(tm, "%d:%d:%d,%d", &h, &m, &s, &ms)

	if num != 4 || err != nil {
		return 0, errors.New("Parsing error: Can not covert `" + tm + "' to milliseconds.")
	}

	msecs = h * 60 * 60 * 1000
	msecs += m * 60 * 1000
	msecs += s * 1000
	msecs += ms

	return msecs, nil
}

/* converts milliseconds to hh:mm:ss,mss */
func msecs_to_time(msecs uint) string {
	var h, m, s, ms uint

	h = msecs / (60 * 60 * 1000)
	msecs = msecs % (60 * 60 * 1000)
	m = msecs / (60 * 1000)
	msecs = msecs % (60 * 1000)
	s = msecs / 1000
	msecs = msecs % 1000
	ms = msecs

	tm := fmt.Sprintf("%02d:%02d:%02d,%03d", h, m, s, ms)

	return tm
}

/* read SubRip (srt) file */
func read_srt(filename string) (*list.List, error) {
	var state int = 0
	var subs *list.List
	var sub *subtitle

	f, err := os.Open(filename)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	r := bufio.NewReader(f)
	subs = list.New()
	sub = new(subtitle)

	for {
		var (
			isprefix bool = true
			err error = nil
			ln, line []byte
		)

		for isprefix && err == nil {
			line, isprefix, err = r.ReadLine()
			if err != nil && err != io.EOF {
				return nil, err
			}
			ln = append(ln, line...)
		}

		/* parse subtitle id */
		if state == 0 {
			/* avoid false-positive parsing error */
			if err == io.EOF && len(ln) == 0 {
				break;
			}
			id := strings.Split(string(ln), " ")
			if len(id) != 1 {
				return nil, errors.New("Parsing error: Wrong file format")
			}
			_, err = strconv.ParseUint(id[0], 10, 0)
			if err != nil {
				return nil, err
				return nil, errors.New("Parsing error: Wrong file format")
			}
			state = 1
		/* parse start, end times */
		} else if state == 1 {
			tm := strings.Split(string(ln), " ")
			if len(tm) != 3 || tm[1] != "-->" {
				return nil, errors.New("Parsing error: Wrong file format")
			}
			sub.start, err = time_to_msecs(tm[0])
			if err != nil {
				return nil, err
			}
			sub.end, err = time_to_msecs(tm[2])
			if err != nil {
				return nil, err
			}
			state = 2
		/* parse the actual subtitle text */
		} else if state == 2 {
			if len(ln) == 0 {
				subs.PushBack(sub)
				sub = new(subtitle)
				state = 0
			} else {
				sub.text += string(ln) + "\r\n"
			}
		}

		if err == io.EOF {
			break;
		}
	}

	return subs, nil
}

/* write SubRip (srt) file */
func write_srt(filename string, subs *list.List) error {
	var id int = 0

	f, err := os.Create(filename)
	if err != nil {
		return err
	}
	defer f.Close()

	w := bufio.NewWriter(f)
	defer w.Flush()

	for e := subs.Front(); e != nil; e = e.Next() {
		id++
		sub := e.Value.(*subtitle)
		fmt.Fprintf(w, "%d\r\n", id)
		fmt.Fprintf(w, "%s --> %s\r\n", msecs_to_time(sub.start), msecs_to_time(sub.end))
		fmt.Fprintf(w, "%s\r\n", sub.text)
	}

	return nil
}

/* synchronize subtitles by knowing the time of the first and the last subtitle.
 * to archive this we must use the linear equation: y = mx + b */
func sync_subs(subs *list.List, synced_first_ms uint, synced_last_ms uint) {
	var slope, yint float64

	desynced_first_ms := subs.Front().Value.(*subtitle).start
	desynced_last_ms := subs.Back().Value.(*subtitle).start

	/* m = (y2 - y1) / (x2 - x1)
	 * m: slope
	 * y2: synced_last_ms
	 * y1: synced_first_ms
	 * x2: desynced_last_ms
	 * x1: desynced_first_ms */
	slope = float64(synced_last_ms - synced_first_ms) / float64(desynced_last_ms - desynced_first_ms)
	/* b = y - mx
	 * b: yint
	 * y: synced_last_ms
	 * m: slope
	 * x: desynced_last_ms */
	yint = float64(synced_last_ms) - slope * float64(desynced_last_ms)

	for e := subs.Front(); e != nil; e = e.Next() {
		sub := e.Value.(*subtitle)
		/* y = mx + b
		 * y: sub.start and sub.end
		 * m: slope
		 * x: sub.start and sub.end
		 * b: yint */
		sub.start = uint(roundFloat64(slope * float64(sub.start) + yint))
		sub.end = uint(roundFloat64(slope * float64(sub.end) + yint))
	}
}

func main() {
	var first_ms, last_ms uint

	var opts struct {
		FirstTm string `short:"f" long:"first-sub" description:"Time of first subtitle"`
		LastTm string `short:"l" long:"last-sub" description:"Time of last subtitle"`
		InputFl string `short:"i" long:"input" description:"Input file" required:"true"`
		OutputFl string `short:"o" long:"output" description:"Output file"`
	}

	_, err := flags.Parse(&opts)
	if err != nil {
		if err.(*flags.Error).Type == flags.ErrHelp {
			fmt.Fprintf(os.Stderr, "Example:\n")
			fmt.Fprintf(os.Stderr, "  %s -f 00:01:33,492 -l 01:39:23,561 -i file.srt\n",
				path.Base(os.Args[0]))
			os.Exit(0)
		}
		os.Exit(1)
	}

	if opts.FirstTm != "" {
		first_ms, err = time_to_msecs(opts.FirstTm)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Please check the value of -f option.\n")
			die(err)
		}
	}

	if opts.LastTm != "" {
		last_ms, err = time_to_msecs(opts.LastTm)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Please check the value of -l option.\n")
			die(err)
		}
	}

	/* if output file is not set, use the input file */
	if opts.OutputFl == "" {
		opts.OutputFl = opts.InputFl
	}

	subs, err := read_srt(opts.InputFl)
	if err != nil {
		die(err)
	}

	/* if time of the first synced subtitle is not set,
	 * use the time of the first desynced subtitle */
	if opts.FirstTm == "" {
		first_ms = subs.Front().Value.(*subtitle).start
	}

	/* if time of the last synced subtitle is not set,
	 * use the time of the last desynced subtitle */
	if opts.LastTm == "" {
		last_ms = subs.Back().Value.(*subtitle).start
	}

	if first_ms > last_ms {
		fmt.Fprintf(os.Stderr, "First subtitle can not be after last subtitle.\n")
		fmt.Fprintf(os.Stderr, "Please check the values of -f and/or -l options.\n")
		os.Exit(1)
	}

	sync_subs(subs, first_ms, last_ms)

	err = write_srt(opts.OutputFl, subs)
	if err != nil {
		die(err)
	}
}