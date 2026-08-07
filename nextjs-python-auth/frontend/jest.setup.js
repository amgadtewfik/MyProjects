import '@testing-library/jest-dom';

// Mock Next.js modules
jest.mock('next/navigation', () => ({
  useRouter: () => ({
    push: jest.fn(),
    replace: jest.fn(),
    prefetch: jest.fn(),
    back: jest.fn(),
    forward: jest.fn(),
    refresh: jest.fn(),
    pathname: '/',
    asPath: '/',
    query: {},
  }),
}));

jest.mock('next/font', () => ({
  google: () => ({
    style: {},
    className: '',
  }),
}));

// Mock fetch globally
global.fetch = jest.fn();
global.window.localStorage = {
  getItem: jest.fn(),
  setItem: jest.fn(),
  removeItem: jest.fn(),
  clear: jest.fn(),
};
