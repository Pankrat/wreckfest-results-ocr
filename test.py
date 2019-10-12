import glob
import os
import os.path
import subprocess


def find_screenshots():
    return glob.glob("test/*.jpg")


def run_conversion(filename):
    os.remove(os.path.splitext(filename)[0] + '.csv')
    subprocess.run(["./convert2csv", filename], stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def check(filename, verbose=True):
    output = os.path.splitext(filename)[0] + '.csv'
    reference = output + '.reference'
    if not os.path.exists(output):
        print("Cannot find {} - program crashed or wrong location?".format(output))
    if verbose:
        print("\n# {}".format(output))
    with open(output) as actual:
        csvdata = actual.readlines()
        if verbose:
            for line in csvdata:
                print(line.strip())

    if os.path.exists(reference):
        with open(reference) as expected:
            count, correct = 0, 0
            expecteddata = expected.readlines()
            for l1, l2 in zip(csvdata, expecteddata):
                d1 = l1.strip().split(',')
                d2 = l2.strip().split(',')
                if l1 == l2 and verbose:
                    print("CORRECT: {}".format(l1.strip()))
                elif verbose:
                    print("WRONG:   {} != {}".format(l1.strip(), l2.strip()))
                for v1, v2 in zip(d1, d2):
                    count += 1
                    if v1 != v2:
                        if verbose:
                            print(" > {} != {}".format(v1, v2))
                    else:
                        correct += 1
        return correct, count
    return 0, 1


if __name__ == "__main__":
    screenshots = find_screenshots()
    for screenshot in screenshots:
        run_conversion(screenshot)
    results = {}
    for screenshot in screenshots:
        results[screenshot] = check(screenshot, verbose=False)
    for screenshot, (correct, count) in results.items():
        print("{:>3}% {}: {}/{}".format(correct * 100 // count, screenshot, correct, count))
